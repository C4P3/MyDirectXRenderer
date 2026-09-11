
#include <Windows.h>
#include <vector>
#include <wrl/client.h> // ComPtr用
#include <string>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <DirectXTex.h>
#include <d3dcompiler.h>

#include "d3dx12.h"
#include "PeraRenderer.h"
#include "Debug.h"
#include "Dx12Wrapper.h"
#include "Scene.h"
#include "RenderGraph/Dx12/Dx12ResourceAllocator.h"


#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "DirectXTex.lib")
#pragma comment(lib, "dxguid.lib")

#ifdef _DEBUG
#include <iostream>
#include <string_view>
#endif

using namespace std;
using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{

	std::vector<float> GetGaussianWeight(size_t count, float s)
	{
		std::vector<float> weights(count); // ウェイト配列返却用
		float x = 0.0f;
		float total = 0.0f;

		for (auto& wgt : weights)
		{
			wgt = expf(-(x * x) / (2 * s * s));
			total += wgt;
			x += 1.0f;
		}

		total = total * 2.0f - 1;

		// 足して 1 になるようにする
		for (auto& wgt : weights)
		{
			wgt /= total;
		}

		return weights;
	}
}


struct PeraVertex
{
	XMFLOAT3 pos;
	XMFLOAT2 uv;
};

// 初期化：シェーダーコンパイル、ルートシグネチャ、PSOの作成を行う
bool PeraRenderer::Init(Dx12ResourceAllocator& allocator)
{
	// dx12.Device() を使ってルートシグネチャやPSOを作成し、
	// メンバ変数の _rootSignature と _pipelineState に格納

	// ペラポリゴン用頂点バッファー(他のRenderではActorの管轄だが、短いのでここに書く)
	PeraVertex pv[4] = {
		{ {-1,-1, 0.1 }, {0, 1} },	// 左下
		{ {-1, 1, 0.1 }, {0, 0} },	// 左上
		{ { 1,-1, 0.1 }, {1, 1} },	// 右下
		{ { 1, 1, 0.1 }, {1, 0} }	// 右上
	};

	HRESULT result;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(pv));
	result = _dx12.Device()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(_peraVB.ReleaseAndGetAddressOf())
	);

	_peraVBV.BufferLocation = _peraVB->GetGPUVirtualAddress();
	_peraVBV.SizeInBytes = sizeof(pv);
	_peraVBV.StrideInBytes = sizeof(PeraVertex);

	PeraVertex* mappedPera = nullptr;
	_peraVB->Map(0, nullptr, (void**)&mappedPera);
	copy(begin(pv), end(pv), mappedPera);
	_peraVB->Unmap(0, nullptr);

	// ぼかしウェイト
	auto weights = GetGaussianWeight(8, 3.0f);
	resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(
		(sizeof(weights[0]) * weights.size() + 0xff) & ~0xff
	);
	result = _dx12.Device()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(_bokehParamBuffer.ReleaseAndGetAddressOf())
	);
	assert(SUCCEEDED(result));

	float* mappedWeight = nullptr;
	result = _bokehParamBuffer->Map(0, nullptr, (void**)&mappedWeight);
	assert(SUCCEEDED(result));
	copy(weights.begin(), weights.end(), mappedWeight);
	_bokehParamBuffer->Unmap(0, nullptr);


	D3D12_DESCRIPTOR_RANGE ranges[2] = {};
	// t0 : 前のパスの結果
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // t
	ranges[0].BaseShaderRegister = 0;  // 0
	ranges[0].NumDescriptors = 1;
	// t1 : 法線マップ
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // t
	ranges[1].BaseShaderRegister = 1;  // 1
	ranges[1].NumDescriptors = 1;

	D3D12_ROOT_PARAMETER rp[3] = {};
	rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rp[0].DescriptorTable.pDescriptorRanges = &ranges[0];
	rp[0].DescriptorTable.NumDescriptorRanges = 1;

	rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;   // ヒープ不要
	rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rp[1].Descriptor.ShaderRegister = 0;   // b0

	rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rp[2].DescriptorTable.pDescriptorRanges = &ranges[1];
	rp[2].DescriptorTable.NumDescriptorRanges = 1;

	D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC(0); // s0

	// ルートシグネチャ
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.NumParameters = 3;
	rootSignatureDesc.pParameters = rp;
	rootSignatureDesc.NumStaticSamplers = 1;
	rootSignatureDesc.pStaticSamplers = &sampler;
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> rootSigBlob = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	result = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1_0,
		&rootSigBlob,
		&errorBlob
	);
	if (FAILED(result)) {
		if (errorBlob) {
			OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
		}
		return DebugFail("PeraRenderer::Init", "ルートシグネチャのシリアライズ", result);
	}
	result = _dx12.Device()->CreateRootSignature(
		0,
		rootSigBlob->GetBufferPointer(),
		rootSigBlob->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignature)
	);
	if (FAILED(result))
		return DebugFail("PeraRenderer::Init", "ルートシグネチャの生成", result);
	rootSigBlob.Reset();

	// ・パイプラインステートオブジェクト(PSO)の作成
	// シェーダーのセット
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};
	gpipeline.pRootSignature = _rootSignature.Get();

	gpipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpipeline.NumRenderTargets = 1;
	gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	gpipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	gpipeline.SampleDesc.Count = 1;
	gpipeline.SampleDesc.Quality = 0;

	// レイアウト
	D3D12_INPUT_ELEMENT_DESC inputLayout[2] = {
		{
			"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
			0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		}
	};
	gpipeline.InputLayout.pInputElementDescs = inputLayout; // レイアウト先頭アドレス
	gpipeline.InputLayout.NumElements = _countof(inputLayout); // レイアウト配列の要素数

	// ・シェーダーのコンパイル
	ComPtr<ID3DBlob> _vsBlob = nullptr;
	ComPtr<ID3DBlob> _psBlob = nullptr;

	// コンパイルとエラー出力を一括で扱うローカル関数
	auto compileShader = [](const wchar_t* fileName, const char* entryPoint, const char* target, ComPtr<ID3DBlob>& outBlob) -> bool {
		ComPtr<ID3DBlob> errorBlob = nullptr;

		HRESULT hr = D3DCompileFromFile(
			fileName,
			nullptr,
			D3D_COMPILE_STANDARD_FILE_INCLUDE,
			entryPoint, target,
			D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
			0,
			&outBlob, &errorBlob
		);

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
				std::string errstr(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
				errstr += "\n";
				::OutputDebugStringA(errstr.c_str());
			}
			return false;
		}
		return true;
		};

	if (!compileShader(L"Shader/peraVertex.hlsl", "vs", "vs_5_0", _vsBlob))
		return DebugFail("PeraRenderer::Init", "peraVertex.hlsl のコンパイル");
	gpipeline.VS = CD3DX12_SHADER_BYTECODE(_vsBlob.Get());

	struct EffectShader { Effect effect; const wchar_t* file; const char* entry; };

	static const EffectShader kEffectShaders[] = {
	{ Effect::BlurHorizontal, L"Shader/HorizontalBokehPS.hlsl", "HorizontalBokehPS" },
	{ Effect::BlurVertical,   L"Shader/VerticalBokehPS.hlsl",   "VerticalBokehPS"   },
	{ Effect::Distortion,     L"Shader/DistortionPS.hlsl",      "DistortionPS"      },
	};

	for (const auto& s : kEffectShaders) {
		if (!compileShader(s.file, s.entry, "ps_5_0", _psBlob))
			return DebugFail("PeraRenderer::Init", "ピクセルシェーダーのコンパイル");
		gpipeline.PS = CD3DX12_SHADER_BYTECODE(_psBlob.Get());
		result = _dx12.Device()->CreateGraphicsPipelineState(
			&gpipeline, IID_PPV_ARGS(&_psos[static_cast<size_t>(s.effect)]));
		if (FAILED(result))
			return DebugFail("PeraRenderer::Init", "エフェクト用 PSO の生成", result);
	}

	DirectX::TexMetadata metadata = {};
	DirectX::ScratchImage scratchImg = {};
	HRESULT hr = DirectX::LoadFromWICFile(
		L"Texture/normalmap.jpg", DirectX::WIC_FLAGS_NONE, &metadata, scratchImg);
	if (FAILED(hr))
		return DebugFail("PeraRenderer::Init", "法線マップの読み込み（Texture/normalmap.jpg）", hr);

	auto img = scratchImg.GetImage(0, 0, 0);
	_normalMap = _dx12.CreateTextureFromData(
		metadata.width, metadata.height, metadata.format,
		img->pixels, img->rowPitch, img->slicePitch);
	if (!_normalMap)
		return DebugFail("PeraRenderer::Init", "法線マップのリソース生成");

	const uint32_t id = allocator.RegisterExternalTexture(_normalMap.Get());
	_normalMapSrv = allocator.SrvOf(id);

	return true;
}

// 描画コマンドの積み込み
void PeraRenderer::Draw(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srv,
	Effect effect)
{
	auto cmdList = _dx12.CommandList();

	cmdList->SetPipelineState(_psos[static_cast<size_t>(effect)].Get());
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	cmdList->SetDescriptorHeaps(1, &srvHeap);                     // t0 も t1 もこの1本の中にある
	cmdList->SetGraphicsRootDescriptorTable(0, srv);
	cmdList->SetGraphicsRootConstantBufferView(1, _bokehParamBuffer->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootDescriptorTable(2, _normalMapSrv);

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	cmdList->IASetVertexBuffers(0, 1, &_peraVBV);
	cmdList->DrawInstanced(4, 1, 0, 0);
}