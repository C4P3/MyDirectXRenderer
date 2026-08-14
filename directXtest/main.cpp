
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

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "DirectXTex.lib")

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

// アライメントに揃えたサイズを返す
// @param size 元のサイズ
// @param alignement アライメントサイズ
// @return アライメントをそろえたサイズ
size_t AlignmentSize(size_t size, size_t alignment)
{
	return size + alignment - size % alignment;
}

// @brief コンソール画面にフォーマット付き文字列を表示
// @param format フォーマット (%d とか %f とかの)
// @param 可変長引数
// @remarks この関数はデバッグ用です。デバッグ時にしか動作しません。
void DebugOutputFormatString(const char* format, ...)
{
#ifdef _DEBUG
	va_list valist;
	va_start(valist, format);
	vprintf(format, valist);
	va_end(valist);
#endif
}

// デバッグレイヤー
void EnableDebugLayer()
{
	ComPtr<ID3D12Debug> debugLayer = nullptr;
	auto result = D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer));

	debugLayer->EnableDebugLayer();
	debugLayer.Reset();
}

// 面倒だけど書かなければいけない関数
LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	// ウィンドウが破棄されたら呼ばれる
	if (msg == WM_DESTROY)
	{
		PostQuitMessage(0); // OS に対して「もうこのアプリアは終わる」と伝える
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam); // 既定の処理を行う
}

// ==========================================
// アプリケーションとウィンドウを管理するクラス
// ==========================================
class Application
{
private:
	HWND hwnd = nullptr;
	WNDCLASSEX w = {};
	int window_width;
	int window_height;

public:
	// コンストラクタ：ウィンドウサイズを保持しておく
	Application(int width, int height) : window_width(width), window_height(height) {}

	// デストラクタ：クラスが破棄される時にウィンドウ登録を解除する
	~Application() {
		if (hwnd) {
			UnregisterClass(w.lpszClassName, w.hInstance);
		}
	}

	// [Region 1: ウィンドウの生成] をここにカプセル化
	bool Init() {
		w.cbSize = sizeof(WNDCLASSEX);
		w.lpfnWndProc = (WNDPROC)WindowProcedure;
		w.lpszClassName = _T("DX12Sample");
		w.hInstance = GetModuleHandle(nullptr);

		RegisterClassEx(&w);

		RECT wrc = { 0, 0, window_width, window_height };
		AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

		hwnd = CreateWindow(
			w.lpszClassName,
			_T("DX12 テスト"),
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			wrc.right - wrc.left,
			wrc.bottom - wrc.top,
			nullptr,
			nullptr,
			w.hInstance,
			nullptr
		);

		if (!hwnd) return false;

		ShowWindow(hwnd, SW_SHOW);
		return true;
	}

	// メインループ前半
	// 戻り値: メッセージを処理したか（trueなら描画せず次のループへ、falseなら描画フェーズへ）
	bool ProcessMessage(bool& quit) {
		MSG msg = {};
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				quit = true;
			}
			else {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			return true; // メッセージがあった（元のコードの if(PeekMessage) 内に相当）
		}
		return false; // メッセージなし（元のコードの else 句に相当、描画タイミング）
	}

	// 外部から必要な情報を取得するためのゲッター
	HWND GetWindowHandle() const { return hwnd; }
	int GetWindowWidth() const { return window_width; }
	int GetWindowHeight() const { return window_height; }
};

class Dx12Wrapper
{
private:
	ComPtr<ID3D12Device> _dev;
	ComPtr<IDXGIFactory6> _dxgiFactory;
	ComPtr<IDXGISwapChain4> _swapchain;
	ComPtr<ID3D12CommandAllocator> _cmdAllocator;
	ComPtr<ID3D12GraphicsCommandList> _cmdList;
	ComPtr<ID3D12CommandQueue> _cmdQueue;
	ComPtr<ID3D12DescriptorHeap> rtvHeaps;
	std::vector<ComPtr<ID3D12Resource>> _backBuffers;
	ComPtr<ID3D12Fence> _fence;
	UINT64 _fenceVal = 0;
	D3D12_VIEWPORT _viewport = {};
	D3D12_RECT _scissorRect = {};
public:
	// Region 3 や Region 4 からアクセスするためのゲッター
	ID3D12Device* Device() const { return _dev.Get(); }
	ID3D12GraphicsCommandList* CommandList() const { return _cmdList.Get(); }
	ID3D12CommandQueue* CommandQueue() const { return _cmdQueue.Get(); }
	ID3D12CommandAllocator* CommandAllocator() const { return _cmdAllocator.Get(); }
	ID3D12Fence* Fence() const { return _fence.Get(); }
	UINT64& FenceVal() { return _fenceVal; }

	// DirectX 基本システムの初期化
	bool Init(HWND hwnd, int window_width, int window_height)
	{
		HRESULT result;
#ifdef _DEBUG
		// デバッグレイヤーをオンに
		EnableDebugLayer();
#endif

		// ファクトリー生成
#ifdef _DEBUG
		result = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&_dxgiFactory));
#else
		result = CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory));
#endif
		if (FAILED(result)) return false;

		std::vector<ComPtr<IDXGIAdapter>> adapters;	// アダプター列挙用
		ComPtr<IDXGIAdapter> tmpAdapter = nullptr;
		for (int i = 0; _dxgiFactory->EnumAdapters(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
			adapters.push_back(tmpAdapter);
		}
		ComPtr<IDXGIAdapter> targetAdapter = nullptr;
		for (auto& adpt : adapters)
		{
			DXGI_ADAPTER_DESC adesc = {};
			adpt->GetDesc(&adesc);
			std::wstring strDesc = adesc.Description; // アダプターの説明オブジェクト取得
			if (strDesc.find(L"AMD") != std::wstring::npos) {// 探したいアダプターの名前を確認
				targetAdapter = adpt;
				break;
			}
		}
		// AMDが見つからなかった場合最初のアダプターを使う
		if (targetAdapter == nullptr && !adapters.empty()) {
			targetAdapter = adapters[0];
		}

		D3D_FEATURE_LEVEL levels[] = {
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0
		};
		// Direct3d デバイスの初期化
		D3D_FEATURE_LEVEL featureLevel;
		for (auto lv : levels)
		{
			if (D3D12CreateDevice(targetAdapter.Get(), lv, IID_PPV_ARGS(&_dev)) == S_OK)
			{
				featureLevel = lv;
				break;// 生成可能なバージョンが見つかったらループ打ち切り
			}
		}

		// コマンドアロケーター
		result = _dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_cmdAllocator));
		if (FAILED(result)) return false;

		// コマンドリスト
		result = _dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _cmdAllocator.Get(), nullptr, IID_PPV_ARGS(&_cmdList));
		if (FAILED(result)) return false;

		// コマンドキュー
		D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
		cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;// タイムアウトなし
		cmdQueueDesc.NodeMask = 0;// アダプターを 1 っしか使わないときは 0 でよい
		cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;// プライオリティは特に指定なし
		cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;// コマンドリストと合わせる
		result = _dev->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&_cmdQueue));// キュー作成
		if (FAILED(result)) return false;

		// スワップチェーン
		DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
		swapchainDesc.Width = window_width;
		swapchainDesc.Height = window_height;
		swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapchainDesc.Stereo = false;
		swapchainDesc.SampleDesc.Count = 1;
		swapchainDesc.SampleDesc.Quality = 0;
		swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapchainDesc.BufferCount = 2;
		swapchainDesc.Scaling = DXGI_SCALING_STRETCH;// バックバッファーは伸び縮み可能
		swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;// フリップ後は速やかに破棄
		swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;// 特に指定なし
		swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;// ウィンドウ⇔フルスクリーン切替可能
		// CreateSwapChainForHwnd は IDXGISwapChain1 しか返せないので、一旦 IDXGISwapChain1 用の ComPtr で受け取るのが安全。
		ComPtr<IDXGISwapChain1> swapchain1;
		result = _dxgiFactory->CreateSwapChainForHwnd(
			_cmdQueue.Get(),
			hwnd,
			&swapchainDesc,
			nullptr,
			nullptr,
			&swapchain1	// ComPtr の & 演算子はポインタのアドレス（**）を自動で返す
		);
		if (FAILED(result)) return false;
		// 取得した IDXGISwapChain1 を IDXGISwapChain4 にアップキャストする
		result = swapchain1.As(&_swapchain);
		if (FAILED(result)) return false;

		// レンダーターゲットビュー
		// ディスクリプタヒープ
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // レンダーターゲットビューなのでRTV
		heapDesc.NodeMask = 0;
		heapDesc.NumDescriptors = 2; // 表裏の２つ
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // 特に指定なし
		result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeaps));
		if (FAILED(result)) return false;

		// スワップチェーンのメモリと紐づけ
		DXGI_SWAP_CHAIN_DESC swcDesc = {};
		result = _swapchain->GetDesc(&swcDesc);
		if (FAILED(result)) return false;

		// backBuffers
		_backBuffers.resize(swcDesc.BufferCount);
		D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
		// SRGB レンダーターゲットビュー設定
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		for (int idx = 0; idx < swcDesc.BufferCount; ++idx)
		{
			result = _swapchain->GetBuffer(idx, IID_PPV_ARGS(&_backBuffers[idx]));
			if (FAILED(result)) return false;
			_dev->CreateRenderTargetView(_backBuffers[idx].Get(), &rtvDesc, handle);
			handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}

		// フェンス
		result = _dev->CreateFence(_fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
		if (FAILED(result)) return false;

		_viewport.Width = static_cast<float>(window_width);
		_viewport.Height = static_cast<float>(window_height);
		_viewport.TopLeftX = 0;
		_viewport.TopLeftY = 0;
		_viewport.MaxDepth = 1.0f;
		_viewport.MinDepth = 0.0f;

		_scissorRect.top = 0;
		_scissorRect.left = 0;
		_scissorRect.right = window_width;
		_scissorRect.bottom = window_height;

		return true;
	}

	// Region 5 の描画前処理
	void BeginDraw()
	{
		auto bbIdx = _swapchain->GetCurrentBackBufferIndex();
		D3D12_RESOURCE_BARRIER BarrierDesc = CD3DX12_RESOURCE_BARRIER::Transition(_backBuffers[bbIdx].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		_cmdList->ResourceBarrier(1, &BarrierDesc);

		auto rtvH = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
		rtvH.ptr += bbIdx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		_cmdList->OMSetRenderTargets(1, &rtvH, true, nullptr);

		float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
		_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

		// ビューポートとシザー矩形
		_cmdList->RSSetViewports(1, &_viewport);
		_cmdList->RSSetScissorRects(1, &_scissorRect);
	}

	// Region 5 の描画後処理とGPU同期
	void EndDraw()
	{
		auto bbIdx = _swapchain->GetCurrentBackBufferIndex();
		D3D12_RESOURCE_BARRIER BarrierDesc = CD3DX12_RESOURCE_BARRIER::Transition(_backBuffers[bbIdx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		_cmdList->ResourceBarrier(1, &BarrierDesc);

		_cmdList->Close();
		ID3D12CommandList* cmdlists[] = { _cmdList.Get() };
		_cmdQueue->ExecuteCommandLists(1, cmdlists);

		_swapchain->Present(1, 0);

		WaitForGPU();

		_cmdAllocator->Reset();
		_cmdList->Reset(_cmdAllocator.Get(), nullptr);
	}

	void WaitForGPU()
	{
		// コマンドキューにシグナルを送る
		_cmdQueue->Signal(_fence.Get(), ++_fenceVal);

		// GPUがシグナルに到達するまで待つ
		if (_fence->GetCompletedValue() != _fenceVal) {
			auto event = CreateEvent(nullptr, false, false, nullptr);
			_fence->SetEventOnCompletion(_fenceVal, event);
			WaitForSingleObject(event, INFINITE);
			CloseHandle(event);
		}
	}

	// 汎用的なバッファ作成関数
	ComPtr<ID3D12Resource> CreateBuffer(size_t sizeInBytes, const void* data)
	{
		ComPtr<ID3D12Resource> buffer = nullptr;
		auto heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto resdesc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);

		HRESULT hr = _dev->CreateCommittedResource(
			&heapprop,
			D3D12_HEAP_FLAG_NONE,
			&resdesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&buffer)
		);
		if (FAILED(hr)) return nullptr;

		if (data != nullptr) {
			void* mappedPtr = nullptr;
			hr = buffer->Map(0, nullptr, &mappedPtr);
			if (SUCCEEDED(hr)) {
				std::memcpy(mappedPtr, data, sizeInBytes);
				buffer->Unmap(0, nullptr);
			}
		}
		return buffer;
	}
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
	// WIC テクスチャのロード
	TexMetadata metadata = {};
	ScratchImage scratchImg = {};
	
	result = LoadFromWICFile(
		L"img/tsukimi_jugoya.png", WIC_FLAGS_NONE,
		&metadata, scratchImg
	);

	auto img = scratchImg.GetImage(0, 0, 0);

	// アップロード用バッファー
	// ヒープ
	// 中間バッファーとしてのアップロードヒープ設定
	D3D12_HEAP_PROPERTIES uploadHeapProp = {};

	// マップ可能にするため、UPLOAD にする
	uploadHeapProp.Type = D3D12_HEAP_TYPE_UPLOAD;

	// アップロード用に使用すること前提なのでUNKNOWN
	uploadHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	uploadHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	uploadHeapProp.CreationNodeMask = 0; // 単一アダプタ
	uploadHeapProp.VisibleNodeMask = 0; // 単一アダプタ

	// リソース設定
	D3D12_RESOURCE_DESC resDesc = {};

	resDesc.Format = DXGI_FORMAT_UNKNOWN;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resDesc.Width = AlignmentSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * img->height;
	resDesc.Height = 1;
	resDesc.DepthOrArraySize = 1;
	resDesc.MipLevels = 1;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	resDesc.SampleDesc.Count = 1;
	resDesc.SampleDesc.Quality = 0;

	// 中間バッファー作成
	ComPtr<ID3D12Resource> uploadbuff = nullptr;
	result = dx12.Device()->CreateCommittedResource(
		&uploadHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadbuff)
	);
	if (FAILED(result)) return -1;

	// テクスチャのためのheap設定
	D3D12_HEAP_PROPERTIES texHeapProp = {};

	texHeapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	texHeapProp.CreationNodeMask = 0;
	texHeapProp.VisibleNodeMask = 0;

	// リソース設定
	resDesc.Format = metadata.format;
	resDesc.Width = metadata.width;
	resDesc.Height = metadata.height;
	resDesc.DepthOrArraySize = metadata.arraySize;
	resDesc.MipLevels = metadata.mipLevels;
	resDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.SampleDesc.Count = 1;
	resDesc.SampleDesc.Quality = 0;
	resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// テクスチャバッファーの作成
	ComPtr<ID3D12Resource> texbuff = nullptr;
	result = dx12.Device()->CreateCommittedResource(
		&texHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&texbuff)
	);

	uint8_t* mapforImg = nullptr;
	// uploadbuffをCPUから書き込めるように「Map（マッピング）」し、
	// その書き込み口のアドレスを mapforImg にもらう。
	result = uploadbuff->Map(0, nullptr, (void**)&mapforImg);

	// 元データのRowPitchとバッファーのRowPitchの違いを1行ごとに行頭合わせ
	auto srcAddress = img->pixels;
	auto rowPitch = AlignmentSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	for (int y = 0; y < img->height; ++y) {
		std::copy_n(srcAddress, img->rowPitch, mapforImg);
		srcAddress += img->rowPitch;
		mapforImg += rowPitch;
	}

	uploadbuff->Unmap(0, nullptr);

	// シェーダリソースビュー
	ComPtr<ID3D12DescriptorHeap> texDescHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};

	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descHeapDesc.NodeMask = 0;
	descHeapDesc.NumDescriptors = 1;
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	result = dx12.Device()->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&texDescHeap));
	if (FAILED(result)) return -1;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	
	dx12.Device()->CreateShaderResourceView(
		texbuff.Get(),
		&srvDesc,
		texDescHeap->GetCPUDescriptorHandleForHeapStart()
	);

	// CopyTextureRegion用
	D3D12_TEXTURE_COPY_LOCATION src = {};

	src.pResource = uploadbuff.Get(); //中間バッファー
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint.Offset = 0;
	src.PlacedFootprint.Footprint.Width = metadata.width;
	src.PlacedFootprint.Footprint.Height = metadata.height;
	src.PlacedFootprint.Footprint.Depth = metadata.depth;
	src.PlacedFootprint.Footprint.RowPitch = AlignmentSize(img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT); // 256 アラインメント
	src.PlacedFootprint.Footprint.Format = img->format;

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	//コピー先設定
	dst.pResource = texbuff.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	D3D12_RESOURCE_BARRIER BarrierDesc = CD3DX12_RESOURCE_BARRIER::Transition(
		texbuff.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	dx12.CommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	dx12.CommandList()->ResourceBarrier(1, &BarrierDesc);
	dx12.CommandList()->Close();

	ID3D12CommandList* cmdlists[] = { dx12.CommandList() };
	dx12.CommandQueue()->ExecuteCommandLists(1, cmdlists);

	dx12.WaitForGPU();

	dx12.CommandAllocator()->Reset();
	dx12.CommandList()->Reset(dx12.CommandAllocator(), nullptr);
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