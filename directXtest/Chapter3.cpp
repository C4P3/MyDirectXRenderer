#include <Windows.h>
#include <tchar.h> // _T マクロ用
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <wrl/client.h> // ComPtr用
#include <string>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#ifdef _DEBUG
#include <iostream>
#endif

using namespace std;
using Microsoft::WRL::ComPtr;

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
	debugLayer.ReleaseAndGetAddressOf();
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

#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif

#pragma region ウィンドウの生成
	// ウィンドウクラスの生成＆登録
	WNDCLASSEX w = {};
	w.cbSize = sizeof(WNDCLASSEX);
	w.lpfnWndProc = (WNDPROC)WindowProcedure;	// コールバック関数の指定
	w.lpszClassName = _T("DX12Sample");			// アプリケーションクラス名
	w.hInstance = GetModuleHandle(nullptr);		// ハンドルの取得

	RegisterClassEx(&w);	// アプリケーションクラス（ウィンドウクラスの指定をOSに伝える）

	int window_width = 1280;
	int window_height = 720;
	RECT wrc = { 0, 0, window_width, window_height };	// ウィンドウサイズを決める

	// 関数を使ってウィンドウのサイズを補正する
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウ
	HWND hwnd = CreateWindow(
		w.lpszClassName,	// クラス名指定
		_T("DX12 テスト"),	// タイトルバーの文字
		WS_OVERLAPPEDWINDOW,// タイトルバーと境界線があるウィンドウ
		CW_USEDEFAULT,		// 表示x座標はOSにお任せ
		CW_USEDEFAULT,		// 表示y座標はOSにお任せ
		wrc.right - wrc.left,	// ウィンドウ幅
		wrc.bottom - wrc.top,	// ウィンドウ高
		nullptr,		// 親ウィンドウハンドル
		nullptr,		// メニューハンドル
		w.hInstance,	// 呼び出しアプリケーションハンドル
		nullptr			// 追加パラメータ
	);

	// ウィンドウ表示
	ShowWindow(hwnd, SW_SHOW);
#pragma endregion ウィンドウの生成


	ComPtr<ID3D12Device> _dev;
	ComPtr<IDXGIFactory6> _dxgiFactory;
	ComPtr<IDXGISwapChain4> _swapchain;
	HRESULT result;

#pragma region Direct3Dの初期化

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
	if (FAILED(result)) return -1;

	// アダプターの列挙用
	std::vector<ComPtr<IDXGIAdapter>> adapters;
	ComPtr<IDXGIAdapter> tmpAdapter = nullptr; // ここに特定の名前を持つアダプターオブジェクトが入る

	// EnumAdaptersは内部でAddRefを呼ぶため、ComPtrで受け取る
	for (int i = 0;
		_dxgiFactory->EnumAdapters(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND;
		++i
		) {
		adapters.push_back(tmpAdapter);
	}

	ComPtr<IDXGIAdapter> targetAdapter = nullptr;

	for (auto& adpt : adapters)
	{
		DXGI_ADAPTER_DESC adesc = {};
		adpt->GetDesc(&adesc);

		std::wstring strDesc = adesc.Description; // アダプターの説明オブジェクト取得

		// 探したいアダプターの名前を確認
		if (strDesc.find(L"AMD") != std::wstring::npos) {
#ifdef _DEBUG
			// wstringの出力には wcout か OutputDebugStringW が安全
			std::wcout << L"Target Adapter found: " << strDesc << std::endl;
#endif
			targetAdapter = adpt;
			break;
		}
	}

	// AMDが見つからなかった場合のフォールバック（最初のアダプターを使う）
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
		// targetAdapter.Get() で生ポインタを取得して渡す
		if (D3D12CreateDevice(targetAdapter.Get(), lv, IID_PPV_ARGS(&_dev)) == S_OK)
		{
			featureLevel = lv;
			break;// 生成可能なバージョンが見つかったらループ打ち切り
		}
	}
#pragma endregion Direct3Dの初期化

#pragma region コマンドリストの作成とコマンドアロケーター
	ComPtr<ID3D12CommandAllocator> _cmdAllocator = nullptr;
	ComPtr<ID3D12GraphicsCommandList> _cmdList = nullptr;

	result = _dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&_cmdAllocator));
	if (FAILED(result)) return -1;

	result = _dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		_cmdAllocator.Get(), nullptr,
		IID_PPV_ARGS(&_cmdList));
	if (FAILED(result)) return -1;
#pragma endregion コマンドリストの作成とコマンドアロケーター

#pragma region コマンドキュー
	ComPtr<ID3D12CommandQueue> _cmdQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};

	// タイムアウトなし
	cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	// アダプターを 1 っしか使わないときは 0 でよい
	cmdQueueDesc.NodeMask = 0;
	// プライオリティは特に指定なし
	cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	// コマンドリストと合わせる
	cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	// キュー作成
	result = _dev->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&_cmdQueue));
	if (FAILED(result)) return -1;
#pragma endregion コマンドキュー

#pragma region スワップチェーン
	// スワップチェーン生成
	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = window_width;
	swapchainDesc.Height = window_height;
	swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainDesc.Stereo = false;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = 2;
	// バックバッファーは伸び縮み可能
	swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
	// フリップ後は速やかに破棄
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	// 特に指定なし
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	// ウィンドウ⇔フルスクリーン切替可能
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// CreateSwapChainForHwnd は IDXGISwapChain1 しか返せないので、
	// 一旦 IDXGISwapChain1 用の ComPtr で受け取るのが安全。
	ComPtr<IDXGISwapChain1> swapchain1;

	result = _dxgiFactory->CreateSwapChainForHwnd(
		_cmdQueue.Get(),
		hwnd,
		&swapchainDesc,
		nullptr,
		nullptr,
		&swapchain1	// ComPtr の & 演算子はポインタのアドレス（**）を自動で返す
	);
	if (FAILED(result)) return -1;

	// 取得した IDXGISwapChain1 を IDXGISwapChain4 にアップキャストする
	result = swapchain1.As(&_swapchain);
	if (FAILED(result)) return -1;
#pragma endregion スワップチェーン

#pragma region レンダーターゲットビュー
	// ディスクリプタヒープ
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};

	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // レンダーターゲットビューなのでRTV
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 2; // 表裏の２つ
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // 特に指定なし

	ComPtr<ID3D12DescriptorHeap> rtvHeaps = nullptr;

	result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeaps));
	if (FAILED(result)) return -1;

	// スワップチェーンのメモリと紐づけ
	DXGI_SWAP_CHAIN_DESC swcDesc = {};

	result = _swapchain->GetDesc(&swcDesc);
	if (FAILED(result)) return -1;

	std::vector<ComPtr<ID3D12Resource>> _backBuffers(swcDesc.BufferCount);
	for (int idx = 0; idx < swcDesc.BufferCount; ++idx)
	{
		result = _swapchain->GetBuffer(idx, IID_PPV_ARGS(&_backBuffers[idx]));
		if (FAILED(result)) return -1;

		D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();

		handle.ptr += idx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		_dev->CreateRenderTargetView(
			_backBuffers[idx].Get(),
			nullptr,
			handle
		);
	}
#pragma endregion レンダーターゲットビュー

#pragma region フェンス
	ComPtr<ID3D12Fence> _fence = nullptr;
	UINT64 _fenceVal = 0;

	result = _dev->CreateFence(_fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
	if (FAILED(result)) return -1;
#pragma endregion フェンス


#pragma region ループ
	MSG msg = {};
	float test = 0.5f;

	while (true) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			// アプリケーションが終わるときに message が WM_QUIT になる
			if (msg.message == WM_QUIT)
			{
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
#pragma region スワップチェーンを動作させる
			// 1. レンダーターゲットの設定
			auto bbIdx = _swapchain->GetCurrentBackBufferIndex();

			D3D12_RESOURCE_BARRIER BarrierDesc = {};
			BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; // 遷移
			BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;	// 特に指定なし
			BarrierDesc.Transition.pResource = _backBuffers[bbIdx].Get(); // バックバッファーリソース
			BarrierDesc.Transition.Subresource = 0;

			// 【バリア1】PRESENT状態 から RENDER_TARGET状態 へ遷移
			BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; // 直前はPRESENT状態
			BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET; // 今からレンダーターゲット状態
			_cmdList->ResourceBarrier(1, &BarrierDesc); // バリア指定実行

			auto rtvH = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
			rtvH.ptr += bbIdx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			_cmdList->OMSetRenderTargets(1, &rtvH, true, nullptr);

			// 2. レンダーターゲットのクリア
			test += 0.01f;
			if (test > 1.0f) test = 0.0f;
			float clearColor[] = { 1.0f, 1.0f, test, 1.0f }; // 黄色
			_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

			// 描画が終わったら、コマンドリストを閉じる「前」に、
			// 画面出力用（PRESENT）の状態に戻すバリアを積む。
			BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			_cmdList->ResourceBarrier(1, &BarrierDesc);

			// 3. ためておいた命令の実行
			// コマンドリストのクローズ
			_cmdList->Close();

			// コマンドリストの実行
			ID3D12CommandList* cmdlists[] = { _cmdList.Get() };
			_cmdQueue->ExecuteCommandLists(1, cmdlists);

			// 4. 画面のスワップ
			// フリップ
			_swapchain->Present(1, 0);

			// 5. GPUの実行完了を待機
			_cmdQueue->Signal(_fence.Get(), ++_fenceVal);
			if (_fence->GetCompletedValue() != _fenceVal) {
				// イベントハンドルの取得
				auto event = CreateEvent(nullptr, false, false, nullptr);

				_fence->SetEventOnCompletion(_fenceVal, event);

				// イベントが発生するまで待ち続ける（INFINITE）
				WaitForSingleObject(event, INFINITE);

				// イベントハンドルを閉じる
				CloseHandle(event);
			}

			// 次のフレームに向けてキュートリストをリセット
			_cmdAllocator->Reset(); // キューをクリア
			_cmdList->Reset(_cmdAllocator.Get(), nullptr); // 再びコマンドリストをためる準備
#pragma endregion スワップチェーンを動作させる
		}
	}

	// もうこのクラスは使わないので登録解除する
	UnregisterClass(w.lpszClassName, w.hInstance);
#pragma endregion ループ

	// ComPtrを使用しているため、スコープを抜ける際に自動的に_devや_dxgiFactoryなどはRelease()される。
	return 0;
}