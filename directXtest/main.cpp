
#include <Windows.h>
#include <tchar.h> // _T マクロ用
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <wrl/client.h> // ComPtr用
#include <string>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <DirectXTex.h>
#include "d3dx12.h"
#include "Application.h"
#include "Dx12Wrapper.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "DirectXTex.lib")
#pragma comment(lib, "dxguid.lib")

#ifdef _DEBUG
#include <iostream>
#endif

using namespace std;
using namespace DirectX;
using Microsoft::WRL::ComPtr;

// 頂点データ構造体
struct Vertex
{
	XMFLOAT3 pos;
	XMFLOAT2 uv;
};

class BasicRenderer
{
private:
	ComPtr<ID3D12RootSignature> _rootSignature;
	ComPtr<ID3D12PipelineState> _pipelineState;

public:
	// 初期化：シェーダーコンパイル、ルートシグネチャ、PSOの作成を行う
	bool Init(Dx12Wrapper& dx12)
	{
		// dx12.Device() を使ってルートシグネチャやPSOを作成し、
		// メンバ変数の _rootSignature と _pipelineState に格納します。

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

		if (!compileShader(L"BasicVertexShader.hlsl", "BasicVS", "vs_5_0", _vsBlob)) return -1;
		if (!compileShader(L"BasicPixelShader.hlsl", "BasicPS", "ps_5_0", _psBlob)) return -1;

		// ・ルートシグネチャの作成
		// ルートシグネチャ
		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};

		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		// ディスクリプタレンジ
		D3D12_DESCRIPTOR_RANGE descTblRange = {};
		descTblRange.NumDescriptors = 1;
		descTblRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descTblRange.BaseShaderRegister = 0;
		descTblRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		// ルートパラメータ作成
		D3D12_ROOT_PARAMETER rootparam = {};
		rootparam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootparam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootparam.DescriptorTable.pDescriptorRanges = &descTblRange;
		rootparam.DescriptorTable.NumDescriptorRanges = 1;

		rootSignatureDesc.pParameters = &rootparam;
		rootSignatureDesc.NumParameters = 1;

		//サンプラーの設定
		D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
		samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		samplerDesc.MinLOD = 0.0f;
		samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

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
			return -1;
		}
		result = dx12.Device()->CreateRootSignature(
			0,
			rootSigBlob->GetBufferPointer(),
			rootSigBlob->GetBufferSize(),
			IID_PPV_ARGS(&_rootSignature)
		);
		if (FAILED(result)) return -1;
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

		gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // カリングしない
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
			{// 座標情報
				"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
				D3D12_APPEND_ALIGNED_ELEMENT,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			},
			{// uv
				"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
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

		gpipeline.NumRenderTargets = 1;
		gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

		gpipeline.SampleDesc.Count = 1;
		gpipeline.SampleDesc.Quality = 0;

		result = dx12.Device()->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(&_pipelineState));
		if (FAILED(result))return -1;

		return true;
	}

	// 描画コマンドの積み込み
	void Draw(Dx12Wrapper& dx12,
		const D3D12_VERTEX_BUFFER_VIEW& vbView,
		const D3D12_INDEX_BUFFER_VIEW& ibView,
		ID3D12DescriptorHeap* texDescHeap,
		int indexCount)
	{
		auto cmdList = dx12.CommandList();

		// パイプラインの設定
		cmdList->SetPipelineState(_pipelineState.Get());
		cmdList->SetGraphicsRootSignature(_rootSignature.Get());

		// テクスチャ（ヒープ）のセット
		ID3D12DescriptorHeap* ppHeaps[] = { texDescHeap };
		cmdList->SetDescriptorHeaps(1, ppHeaps);
		cmdList->SetGraphicsRootDescriptorTable(0, texDescHeap->GetGPUDescriptorHandleForHeapStart());

		// ジオメトリのセットと描画
		cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmdList->IASetVertexBuffers(0, 1, &vbView);
		cmdList->IASetIndexBuffer(&ibView);

		// インデックス数を指定して描画
		cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
	}
};

#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif

#pragma region 1. ウィンドウの生成
	Application app(1280, 720);
	if (!app.Init()) {
		return -1;
	}

	int window_width = app.GetWindowWidth();
	int window_height = app.GetWindowHeight();
	HWND hwnd = app.GetWindowHandle();
#pragma endregion ウィンドウの生成

	Dx12Wrapper dx12;
	if (!dx12.Init(app.GetWindowHandle(), app.GetWindowWidth(), app.GetWindowHeight())) {
		return -1;
	}

	HRESULT result;
#pragma region 3. パイプラインの構築
	BasicRenderer renderer;
	renderer.Init(dx12); // パイプライン構築
#pragma endregion 3. パイプラインの構築

#pragma region 4. アセットの作成とデータ転送

	// 頂点バッファー
	Vertex vertices[] =
	{
		{{-0.4f, -0.7f, 0.0f}, {0.0f, 1.0f}},
		{{-0.4f, 0.7f, 0.0f}, {0.0f, 0.0f}},
		{{0.4f, -0.7f, 0.0f}, {1.0f, 1.0f}},
		{{0.4f, 0.7f, 0.0f}, {1.0f, 0.0f}}
	};
	ComPtr<ID3D12Resource> vertBuff = dx12.CreateBuffer(sizeof(vertices), vertices);

	// 頂点バッファービュー
	D3D12_VERTEX_BUFFER_VIEW vbView = {};
	vbView.BufferLocation = vertBuff->GetGPUVirtualAddress(); // バッファーの仮想アドレス
	vbView.SizeInBytes = sizeof(vertices);	// 全バイト数
	vbView.StrideInBytes = sizeof(vertices[0]);	// 一頂点辺りのバイト数

	// インデックスバッファー
	unsigned short indices[] = {
		0, 1, 2,
		2, 1, 3
	};
	ComPtr<ID3D12Resource> idxBuff = dx12.CreateBuffer(sizeof(indices), indices);

	// インデックスバッファービューを作成
	D3D12_INDEX_BUFFER_VIEW ibView = {};
	ibView.BufferLocation = idxBuff->GetGPUVirtualAddress();
	ibView.Format = DXGI_FORMAT_R16_UINT;
	ibView.SizeInBytes = sizeof(indices);
	
	//画像
	// --- 画像のロードとテクスチャリソース・SRVの作成 ---
	ComPtr<ID3D12DescriptorHeap> texDescHeap = nullptr;
	ComPtr<ID3D12Resource> texbuff = dx12.CreateTextureFromFile(L"img/tsukimi_jugoya.png", texDescHeap);

	if (!texbuff) {
		return -1; // ロード失敗時
	}
#pragma endregion region 4. アセットの作成とデータ転送

#pragma region 5. メインループ
	bool quit = false;
	while (!quit) {
		if (app.ProcessMessage(quit)) {
			continue;
		}
		else
		{
			// ========= 描画前処理 =========
			dx12.BeginDraw();

			// ========= 実際の描画 (ここだけ残る) =========
			renderer.Draw(dx12, vbView, ibView, texDescHeap.Get(), 6);

			// ========= 描画後処理とGPU同期 =========
			dx12.EndDraw();
		}
	}
#pragma endregion 5. メインループ
	return 0;
}