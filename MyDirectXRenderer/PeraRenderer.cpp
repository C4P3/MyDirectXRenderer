
#include <Windows.h>
#include <vector>
#include <wrl/client.h> // ComPtr用
#include <string>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>

#include "d3dx12.h"
#include "PeraRenderer.h"
#include "Dx12Wrapper.h"
#include "Scene.h"


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
bool PeraRenderer::Init()
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

	if (!compileShader(L"peraVertex.hlsl", "vs", "vs_5_0", _vsBlob)) return false;
	if (!compileShader(L"peraPixel.hlsl", "ps", "ps_5_0", _psBlob)) return false;

	D3D12_DESCRIPTOR_RANGE range = {};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // t
	range.BaseShaderRegister = 0;  // 0
	range.NumDescriptors = 1;

	D3D12_ROOT_PARAMETER rp[2] = {};
	rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rp[0].DescriptorTable.pDescriptorRanges = &range;
	rp[0].DescriptorTable.NumDescriptorRanges = 1;

	rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;   // ヒープ不要
	rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rp[1].Descriptor.ShaderRegister = 0;   // b0

	D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC(0); // s0

	// ルートシグネチャ
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.NumParameters = 2;
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
		return false;
	}
	result = _dx12.Device()->CreateRootSignature(
		0,
		rootSigBlob->GetBufferPointer(),
		rootSigBlob->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignature)
	);
	if (FAILED(result)) return false;
	rootSigBlob.Reset();

	// ・パイプラインステートオブジェクト(PSO)の作成
	// シェーダーのセット
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};
	gpipeline.pRootSignature = _rootSignature.Get();

	gpipeline.VS = CD3DX12_SHADER_BYTECODE(_vsBlob.Get());
	gpipeline.PS = CD3DX12_SHADER_BYTECODE(_psBlob.Get());

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

	// 1枚目用パイプライン生成
	result = _dx12.Device()->CreateGraphicsPipelineState(
		&gpipeline, IID_PPV_ARGS(&_psoHorizontal)
	);
	if (FAILED(result))return false;

	// 2枚目用ピクセルシェーダー
	if (!compileShader(L"VerticalBokehPS.hlsl", "VerticalBokehPS", "ps_5_0", _psBlob)) return false;
	gpipeline.PS = CD3DX12_SHADER_BYTECODE(_psBlob.Get());

	// 2枚目用パイプライン生成
	result = _dx12.Device()->CreateGraphicsPipelineState(
		&gpipeline, IID_PPV_ARGS(&_psoVertical)
	);
	if (FAILED(result))return false;

	return true;
}

// 描画コマンドの積み込み
void PeraRenderer::Draw(UINT srvIndex, ID3D12PipelineState* pso)
{
	auto cmdList = _dx12.CommandList();

	cmdList->SetPipelineState(pso);
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	auto heap = _dx12.PeraSRVHeap().Get();
	cmdList->SetDescriptorHeaps(1, &heap);                     // ヒープをセット

	auto handle = heap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += srvIndex * _dx12.Device()->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);// srvIndex番に関連付ける

	cmdList->SetGraphicsRootDescriptorTable(0, handle);
	cmdList->SetGraphicsRootConstantBufferView(1, _bokehParamBuffer->GetGPUVirtualAddress());

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	cmdList->IASetVertexBuffers(0, 1, &_peraVBV);
	cmdList->DrawInstanced(4, 1, 0, 0);
}

void PeraRenderer::DrawHorizontal() { Draw(0, _psoHorizontal.Get()); } // 1枚目を読む
void PeraRenderer::DrawVertical() { Draw(1, _psoVertical.Get()); }   // 2枚目を読む