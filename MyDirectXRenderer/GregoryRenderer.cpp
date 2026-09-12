
#include <Windows.h>
#include <vector>
#include <wrl/client.h> // ComPtr用
#include <string>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>

#include "d3dx12.h"
#include "GregoryRenderer.h"
#include "Debug.h"
#include "GregoryActor.h"
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


// 初期化：シェーダーコンパイル、ルートシグネチャ、PSOの作成を行う
bool GregoryRenderer::Init()
{
	// dx12.Device() を使ってルートシグネチャやPSOを作成し、
	// メンバ変数の _rootSignature と _pipelineState に格納

	HRESULT result;

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

	if (!compileShader(L"Shader/GregoryVertexShader.hlsl", "GregoryVS", "vs_5_0", _vsBlob))
		return DebugFail("GregoryRenderer::Init", "GregoryVertexShader.hlsl のコンパイル");
	if (!compileShader(L"Shader/GregoryPixelShader.hlsl", "GregoryPS", "ps_5_0", _psBlob))
		return DebugFail("GregoryRenderer::Init", "GregoryPixelShader.hlsl のコンパイル");

	// t4 シャドウマップ。レジスタ番号は ShadowShaderHeader.hlsli に合わせる
	D3D12_DESCRIPTOR_RANGE shadowRange = {};
	shadowRange.NumDescriptors = 1;
	shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	shadowRange.BaseShaderRegister = 4;
	shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootparam[3] = {};
	// [0] b0 シーン ＝ ルートCBV
	rootparam[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootparam[0].Descriptor.ShaderRegister = 0;
	rootparam[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// [1] b2 ワールド行列 ＝ ルートCBV
	rootparam[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootparam[1].Descriptor.ShaderRegister = 2;
	rootparam[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// [2] t4 シャドウマップ ＝ テーブル
	rootparam[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootparam[2].DescriptorTable.pDescriptorRanges = &shadowRange;
	rootparam[2].DescriptorTable.NumDescriptorRanges = 1;
	rootparam[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// s2 の比較サンプラー
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


	// ルートシグネチャ
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};

	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = rootparam;
	rootSignatureDesc.NumParameters = _countof(rootparam);

	rootSignatureDesc.pStaticSamplers = &samplerDesc;
	rootSignatureDesc.NumStaticSamplers = 1;

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
		return DebugFail("GregoryRenderer::Init", "ルートシグネチャのシリアライズ", result);
	}
	result = _dx12.Device()->CreateRootSignature(
		0,
		rootSigBlob->GetBufferPointer(),
		rootSigBlob->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignature)
	);
	if (FAILED(result))
		return DebugFail("GregoryRenderer::Init", "ルートシグネチャの生成", result);
	rootSigBlob.Reset();

	// ・パイプラインステートオブジェクト(PSO)の作成
	// シェーダーのセット
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};
	gpipeline.pRootSignature = _rootSignature.Get();

	gpipeline.VS.pShaderBytecode = _vsBlob->GetBufferPointer();
	gpipeline.VS.BytecodeLength = _vsBlob->GetBufferSize();
	gpipeline.PS.pShaderBytecode = _psBlob->GetBufferPointer();
	gpipeline.PS.BytecodeLength = _psBlob->GetBufferSize();

	// サンプルマスクとラスタライザーステート
	// デフォルトのサンプルマスクを表す定数 (0xffffffff)
	gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// まだアンチエイリアスを使わないため false
	gpipeline.RasterizerState.MultisampleEnable = false;

	gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; // カリングは巻き順から裏
	gpipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; // 中身塗りつぶし
	gpipeline.RasterizerState.DepthClipEnable = true; // 深度方向のクリッピングは有効に

	gpipeline.BlendState.AlphaToCoverageEnable = false;
	gpipeline.BlendState.IndependentBlendEnable = false;

	D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {};
	renderTargetBlendDesc.BlendEnable = false;
	renderTargetBlendDesc.LogicOpEnable = false;
	renderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	gpipeline.BlendState.RenderTarget[0] = renderTargetBlendDesc;

	// 頂点レイアウト
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{// 12バイト
			"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{// 12バイト
			"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,
			0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		}
	};

	// ・ビューポートとシザー矩形の設定
	gpipeline.InputLayout.pInputElementDescs = inputLayout; // レイアウト先頭アドレス
	gpipeline.InputLayout.NumElements = _countof(inputLayout); // レイアウト配列の要素数

	gpipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;

	//三角形で構成
	gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	gpipeline.NumRenderTargets = 3;
	gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	gpipeline.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;   // 法線
	gpipeline.RTVFormats[2] = DXGI_FORMAT_R16G16B16A16_FLOAT; // 高輝度（1.0 超えを残す）

	gpipeline.SampleDesc.Count = 1;
	gpipeline.SampleDesc.Quality = 0;

	// 深度バッファー
	gpipeline.DepthStencilState.DepthEnable = true;
	gpipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; // 書き込む
	gpipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS; // 小さいほうを採用

	gpipeline.DepthStencilState.StencilEnable = false;

	gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;


	result = _dx12.Device()->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(&_pipelineState));
	if (FAILED(result))
		return DebugFail("GregoryRenderer::Init", "PSO の生成", result);


	// --- 影用 PSO：ルートシグネチャも入力レイアウトも同じ。VS を差し替えて RT を外すだけ ---
	ComPtr<ID3DBlob> shadowVsBlob = nullptr;
	if (!compileShader(L"Shader/GregoryShadowVS.hlsl", "GregoryShadowVS", "vs_5_0", shadowVsBlob))
		return DebugFail("GregoryRenderer::Init", "GregoryShadowVS.hlsl のコンパイル");

	gpipeline.VS.pShaderBytecode = shadowVsBlob->GetBufferPointer();
	gpipeline.VS.BytecodeLength = shadowVsBlob->GetBufferSize();

	// ピクセルシェーダー無し。深度しか書かないのでカラーも 0 枚
	gpipeline.PS.pShaderBytecode = nullptr;
	gpipeline.PS.BytecodeLength = 0;
	gpipeline.NumRenderTargets = 0;
	// NumRenderTargets 以上の添字は全部 UNKNOWN でないといけない。
	// 色用 PSO の設定を使い回しているので、MRT 化で増えた [1] も戻す。
	for (UINT i = 0; i < _countof(gpipeline.RTVFormats); ++i)
	{
		gpipeline.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
	}

	result = _dx12.Device()->CreateGraphicsPipelineState(
		&gpipeline, IID_PPV_ARGS(&_shadowPipelineState));
	if (FAILED(result))
		return DebugFail("GregoryRenderer::Init", "影用 PSO の生成", result);

	return true;
}

// 描画コマンドの積み込み
void GregoryRenderer::Draw(const Scene& scene, D3D12_GPU_DESCRIPTOR_HANDLE shadowMapSrv)
{
	auto cmdList = _dx12.CommandList();

	// パイプラインの設定
	cmdList->SetPipelineState(_pipelineState.Get());
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	cmdList->SetGraphicsRootConstantBufferView(0, scene.SceneCBAddress());

	// ヒープのバインドはパスの頭（Dx12CommandContext::BeginPass）で済んでいる
	cmdList->SetGraphicsRootDescriptorTable(2, shadowMapSrv);

	for (auto& actor : _actors) actor->Draw();
}

void GregoryRenderer::DrawShadow(const Scene& scene)
{
	auto cmdList = _dx12.CommandList();

	cmdList->SetPipelineState(_shadowPipelineState.Get());
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());
	cmdList->SetGraphicsRootConstantBufferView(0, scene.SceneCBAddress());

	// PMD と違ってマテリアルで分けていないので、影パスも通常パスと同じ描画でよい。
	// PSO だけ差し替わっているので、出るのはライトから見た深度だけ
	for (auto& actor : _actors) actor->Draw();
}