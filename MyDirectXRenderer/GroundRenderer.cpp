#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <string>
#include <wrl/client.h>

#include "d3dx12.h"
#include "Debug.h"
#include "Dx12Wrapper.h"
#include "GroundRenderer.h"
#include "Scene.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d12.lib")

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {

// 板の半径。シャドウマップの範囲より広くてよい（外は影にならないだけ）
constexpr float kHalfSize = 50.0f;

// トライアングルストリップ 4 頂点。裏表は気にしないのでカリングは切る
const XMFLOAT3 kVertices[4] = {
	{ -kHalfSize, 0.0f, -kHalfSize },
	{ -kHalfSize, 0.0f,  kHalfSize },
	{  kHalfSize, 0.0f, -kHalfSize },
	{  kHalfSize, 0.0f,  kHalfSize },
};

bool CompileShader(const wchar_t* fileName, const char* entryPoint, const char* target,
	ComPtr<ID3DBlob>& outBlob) {
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3DCompileFromFile(
		fileName, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entryPoint, target, D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0,
		&outBlob, &errorBlob);

	if (FAILED(hr)) {
		::OutputDebugStringA("[FAIL] シェーダーのコンパイル: ");
		::OutputDebugStringW(fileName);
		::OutputDebugStringA(" / ");
		::OutputDebugStringA(entryPoint);
		::OutputDebugStringA("\n");
		if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
			::OutputDebugStringA("ファイルが見当たりません\n");
		}
		else if (errorBlob) {
			std::string errstr(static_cast<const char*>(errorBlob->GetBufferPointer()),
				errorBlob->GetBufferSize());
			errstr += "\n";
			::OutputDebugStringA(errstr.c_str());
		}
		return false;
	}
	return true;
}

}  // namespace

bool GroundRenderer::Init()
{
	// --- 頂点バッファ ---
	_vertBuff = _dx12.CreateBuffer(sizeof(kVertices), kVertices);
	if (!_vertBuff) return DebugFail("GroundRenderer::Init", "頂点バッファの生成");

	_vbView.BufferLocation = _vertBuff->GetGPUVirtualAddress();
	_vbView.SizeInBytes = sizeof(kVertices);
	_vbView.StrideInBytes = sizeof(XMFLOAT3);

	// --- シェーダー ---
	ComPtr<ID3DBlob> vsBlob, psBlob;
	if (!CompileShader(L"Shader/GroundVertexShader.hlsl", "GroundVS", "vs_5_0", vsBlob))
		return DebugFail("GroundRenderer::Init", "GroundVertexShader.hlsl のコンパイル");
	if (!CompileShader(L"Shader/GroundPixelShader.hlsl", "GroundPS", "ps_5_0", psBlob))
		return DebugFail("GroundRenderer::Init", "GroundPixelShader.hlsl のコンパイル");

	// --- ルートシグネチャ ---
	// b0 シーンと t4 シャドウマップだけ。ワールド行列もマテリアルも持たない
	D3D12_DESCRIPTOR_RANGE shadowRange = {};
	shadowRange.NumDescriptors = 1;
	shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	shadowRange.BaseShaderRegister = 4; // t4
	shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootparam[2] = {};
	rootparam[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootparam[0].Descriptor.ShaderRegister = 0;
	rootparam[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	rootparam[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootparam[1].DescriptorTable.pDescriptorRanges = &shadowRange;
	rootparam[1].DescriptorTable.NumDescriptorRanges = 1;
	rootparam[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// s2 の比較サンプラー。番号は ShadowShaderHeader.hlsli に合わせる
	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MinLOD = 0.0f;
	samplerDesc.ShaderRegister = 2;
	samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = rootparam;
	rootSignatureDesc.NumParameters = _countof(rootparam);
	rootSignatureDesc.pStaticSamplers = &samplerDesc;
	rootSignatureDesc.NumStaticSamplers = 1;

	ComPtr<ID3DBlob> rootSigBlob, errorBlob;
	HRESULT result = D3D12SerializeRootSignature(
		&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootSigBlob, &errorBlob);
	if (FAILED(result)) {
		if (errorBlob) OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
		return DebugFail("GroundRenderer::Init", "ルートシグネチャのシリアライズ", result);
	}
	result = _dx12.Device()->CreateRootSignature(
		0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignature));
	if (FAILED(result))
		return DebugFail("GroundRenderer::Init", "ルートシグネチャの生成", result);

	// --- PSO ---
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
		  D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};
	gpipeline.pRootSignature = _rootSignature.Get();
	gpipeline.VS.pShaderBytecode = vsBlob->GetBufferPointer();
	gpipeline.VS.BytecodeLength = vsBlob->GetBufferSize();
	gpipeline.PS.pShaderBytecode = psBlob->GetBufferPointer();
	gpipeline.PS.BytecodeLength = psBlob->GetBufferSize();
	gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	gpipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // 板なので裏表を気にしない
	gpipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	gpipeline.InputLayout.pInputElementDescs = inputLayout;
	gpipeline.InputLayout.NumElements = _countof(inputLayout);
	gpipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpipeline.NumRenderTargets = 2;
	gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	gpipeline.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gpipeline.SampleDesc.Count = 1;
	gpipeline.DepthStencilState.DepthEnable = true;
	gpipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	gpipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	gpipeline.DepthStencilState.StencilEnable = false;
	gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;

	result = _dx12.Device()->CreateGraphicsPipelineState(
		&gpipeline, IID_PPV_ARGS(&_pipelineState));
	if (FAILED(result))
		return DebugFail("GroundRenderer::Init", "PSO の生成", result);

	return true;
}

void GroundRenderer::Draw(const Scene& scene, D3D12_GPU_DESCRIPTOR_HANDLE shadowMapSrv)
{
	auto cmdList = _dx12.CommandList();

	cmdList->SetPipelineState(_pipelineState.Get());
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());
	cmdList->SetGraphicsRootConstantBufferView(0, scene.SceneCBAddress());

	// ヒープのバインドはパスの頭（Dx12CommandContext::BeginPass）で済んでいる
	cmdList->SetGraphicsRootDescriptorTable(1, shadowMapSrv);

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	cmdList->IASetVertexBuffers(0, 1, &_vbView);
	cmdList->DrawInstanced(4, 1, 0, 0);
}
