#include "Dx12Wrapper.h"

#include <string>
#include <DirectXTex.h>
#include "d3dx12.h"

// ソースファイル内であれば using namespace を使っても安全です
using namespace std;
using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
    void EnableDebugLayer()
    {
        ComPtr<ID3D12Debug> debugLayer = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer)))) {
            debugLayer->EnableDebugLayer();
        }
    }
}

// ==========================================
// 初期化
// ==========================================
bool Dx12Wrapper::Init(HWND hwnd, int window_width, int window_height)
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

	// 深度バッファーの作成
	D3D12_RESOURCE_DESC depthResDesc = {};
	depthResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2次元のテクスチャデータ
	depthResDesc.Width = window_width;
	depthResDesc.Height = window_height;
	depthResDesc.DepthOrArraySize = 1; // テクスチャ配列でも、3Dテクスチャでもない
	depthResDesc.Format = DXGI_FORMAT_D32_FLOAT; // 深度値書き込み用フォーマット
	depthResDesc.SampleDesc.Count = 1; // サンプル数は1ピクセルあたり1つ
	depthResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // デプスステンシルとして使用

	// 深度値用ヒーププロパティ
	D3D12_HEAP_PROPERTIES depthHeapProp = {};
	depthHeapProp.Type = D3D12_HEAP_TYPE_DEFAULT; // DEFAULT なのであとは UNKNOWN でよい
	depthHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	depthHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// このクリアバリューが重要な意味を持つ
	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.DepthStencil.Depth = 1.0f; // 深さ 1.0f (最大値）でクリア
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT; // 32ビットfloat値としてクリア

	result = _dev->CreateCommittedResource(
		&depthHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&depthResDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値書き込みに使用
		&depthClearValue,
		IID_PPV_ARGS(&_depthBuffer)
	);
	if (FAILED(result)) return -1;

	// 深度バッファービューの作成
	// 深度のためのディスクリプタヒープ
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {}; // 深度に使う
	dsvHeapDesc.NumDescriptors = 1; // 深度ビューは1つ
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; // デプスステンシルビューとして使う
	result = _dev->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));
	if (FAILED(result)) return -1;

	// デプス深度ビュー作成
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT; // 深度値に32ビット使用
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2D テクスチャ
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE; // フラグは特になし

	_dev->CreateDepthStencilView(
		_depthBuffer.Get(),
		&dsvDesc,
		dsvHeap->GetCPUDescriptorHandleForHeapStart()
	);

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


// ==========================================
// 描画制御
// ==========================================
void Dx12Wrapper::BeginDraw()
{
	auto bbIdx = _swapchain->GetCurrentBackBufferIndex();
	D3D12_RESOURCE_BARRIER BarrierDesc = CD3DX12_RESOURCE_BARRIER::Transition(_backBuffers[bbIdx].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	_cmdList->ResourceBarrier(1, &BarrierDesc);

	auto rtvH = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
	rtvH.ptr += bbIdx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	auto dsvH = dsvHeap->GetCPUDescriptorHandleForHeapStart();
	_cmdList->OMSetRenderTargets(1, &rtvH, true, &dsvH);

	float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);
	_cmdList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// ビューポートとシザー矩形
	_cmdList->RSSetViewports(1, &_viewport);
	_cmdList->RSSetScissorRects(1, &_scissorRect);
}

void Dx12Wrapper::EndDraw()
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

void Dx12Wrapper::WaitForGPU()
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

// ==========================================
// リソース生成
// ==========================================
ComPtr<ID3D12Resource> Dx12Wrapper::CreateBuffer(size_t sizeInBytes, const void* data, size_t dataSize /*= 0*/)
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
		// CPUから読み込まないことを明確にするため Range(0, 0) を指定
		CD3DX12_RANGE readRange(0, 0);
		hr = buffer->Map(0, &readRange, &mappedPtr);
		
		if (SUCCEEDED(hr)) {
			// dataSize が指定されていなければ sizeInBytes を使用
			size_t copySize = (dataSize > 0) ? dataSize : sizeInBytes;
			std::memcpy(mappedPtr, data, copySize);

			// 書き込んだ範囲を指定して Unmap (nullptr でも可)
			CD3DX12_RANGE writeRange(0, copySize);
			buffer->Unmap(0, &writeRange);
		}
	}
	return buffer;
}

ComPtr<ID3D12Resource> Dx12Wrapper::CreateTextureFromFile(
	const wchar_t* filePath
)
{
	HRESULT result;

	// 1. WIC テクスチャのロード
	TexMetadata metadata = {};
	ScratchImage scratchImg = {};
	result = LoadFromWICFile(filePath, WIC_FLAGS_NONE, &metadata, scratchImg);
	if (FAILED(result)) return nullptr;

	auto img = scratchImg.GetImage(0, 0, 0);

	// 2. テクスチャバッファー（コピー先）の作成
	D3D12_HEAP_PROPERTIES texHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		metadata.format, metadata.width, metadata.height,
		metadata.arraySize, metadata.mipLevels
	);

	ComPtr<ID3D12Resource> texbuff = nullptr;
	result = _dev->CreateCommittedResource(
		&texHeapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texbuff)
	);
	if (FAILED(result)) return nullptr;

	// 3. 中間バッファー（アップロード用）の作成
	// d3dx12.h の機能を使って、必要な中間バッファサイズを自動計算
	UINT64 uploadBufferSize = GetRequiredIntermediateSize(texbuff.Get(), 0, 1);
	D3D12_HEAP_PROPERTIES uploadHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

	ComPtr<ID3D12Resource> uploadbuff = nullptr;
	result = _dev->CreateCommittedResource(
		&uploadHeapProp, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadbuff)
	);
	if (FAILED(result)) return nullptr;

	// 4. データのコピーコマンドを積む
	// UpdateSubresources (d3dx12.h) を使うと、Map/UnmapやRowPitchの計算を自動でやってくれます
	D3D12_SUBRESOURCE_DATA subresourceData = {};
	subresourceData.pData = img->pixels;
	subresourceData.RowPitch = img->rowPitch;
	subresourceData.SlicePitch = img->slicePitch;

	UpdateSubresources(
		_cmdList.Get(), texbuff.Get(), uploadbuff.Get(),
		0, 0, 1, &subresourceData
	);

	// 5. バリアを張ってシェーダーリソース状態に変更
	D3D12_RESOURCE_BARRIER BarrierDesc = CD3DX12_RESOURCE_BARRIER::Transition(
		texbuff.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	_cmdList->ResourceBarrier(1, &BarrierDesc);

	// 6. コマンドを実行して転送完了を待機 (中間バッファを安全に破棄するため)
	_cmdList->Close();
	ID3D12CommandList* cmdlists[] = { _cmdList.Get() };
	_cmdQueue->ExecuteCommandLists(1, cmdlists);
	WaitForGPU(); // ここで待つので、関数を抜けて uploadbuff が消滅しても安全

	// コマンドリストをリセット（次の描画等に備える）
	_cmdAllocator->Reset();
	_cmdList->Reset(_cmdAllocator.Get(), nullptr);

	return texbuff;
}