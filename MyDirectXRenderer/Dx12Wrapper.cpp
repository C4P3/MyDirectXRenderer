#include "Dx12Wrapper.h"

#include <string>
#include <assert.h>
#include "d3dx12.h"

// ソースファイル内であれば using namespace を使っても安全です
using namespace std;
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

	// レンダーターゲットビュー
	// ディスクリプタヒープ
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // レンダーターゲットビューなのでRTV
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 2; // 表裏の２つ
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // 特に指定なし
	result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_rtvDescHeap));
	if (FAILED(result)) return false;

	// スワップチェーンのメモリと紐づけ
	DXGI_SWAP_CHAIN_DESC swcDesc = {};
	result = _swapchain->GetDesc(&swcDesc);
	if (FAILED(result)) return false;

	// backBuffers
	_backBuffers.resize(swcDesc.BufferCount);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = _rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
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

	// マルチパスレンダリング用

	// imgui用
	_heapForImgui = CreateDescriptorHeapForImgui();
	if (_heapForImgui == nullptr) return false;

	return true;
}

void Dx12Wrapper::EndDraw()
{
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

ComPtr<ID3D12Resource> Dx12Wrapper::CreateTextureFromData(
	UINT64 width,
	UINT height,
	DXGI_FORMAT format,
	const void* pixels,
	size_t rowPitch,
	size_t slicePitch
)
{
	D3D12_HEAP_PROPERTIES texHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0);

	D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		format, width, height, 1, 1 // arraySize = 1, mipLevels = 1
	);

	// バッファー作成
	ComPtr<ID3D12Resource> texBuff = nullptr;
	auto result = _dev->CreateCommittedResource(
		&texHeapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&texBuff)
	);
	if (FAILED(result)) return nullptr;

	result = texBuff->WriteToSubresource(
		0, nullptr, pixels,
		static_cast<UINT>(rowPitch),
		static_cast<UINT>(slicePitch)
	);
	if (FAILED(result)) return nullptr;

	return texBuff;
}

ComPtr<ID3D12Resource> Dx12Wrapper::CreateSolidColorTexture(
	uint8_t r, uint8_t g, uint8_t b, uint8_t a/* =255*/
)
{
	uint8_t data[4 * 4 * 4];
	for (size_t i = 0; i < sizeof(data); i += 4) {
		data[i + 0] = r;
		data[i + 1] = g;
		data[i + 2] = b;
		data[i + 3] = a;
	}

	return CreateTextureFromData(
		4,
		4,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		data,
		4 * 4,	// 1ラインサイズ
		sizeof(data)	// 全サイズ
	);
}

ComPtr<ID3D12DescriptorHeap> Dx12Wrapper::CreateDescriptorHeapForImgui() {
	ComPtr<ID3D12DescriptorHeap> ret;
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	desc.NodeMask = 0;
	desc.NumDescriptors = 1;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	_dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(ret.ReleaseAndGetAddressOf()));
	return ret;
};