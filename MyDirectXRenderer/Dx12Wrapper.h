#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <wrl/client.h> // ComPtr用

class Dx12Wrapper
{
private:
    // ヘッダー内なので using namespace を避け、Microsoft::WRL::ComPtr とフルで書く
    Microsoft::WRL::ComPtr<ID3D12Device> _dev;
    Microsoft::WRL::ComPtr<IDXGIFactory6> _dxgiFactory;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> _swapchain;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> _cmdAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _cmdList;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> _cmdQueue;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _rtvDescHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _dsvDescHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _peraRTVHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _peraSRVHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeapForImgui();
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _heapForImgui;


    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> _backBuffers;
    Microsoft::WRL::ComPtr<ID3D12Resource> _depthBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> _peraResource;

    Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
    UINT64 _fenceVal = 0;

    D3D12_VIEWPORT _viewport = {};
    D3D12_RECT _scissorRect = {};

    bool CreateMultiPassResource();
public:
    ID3D12Device* Device() const { return _dev.Get(); }
    ID3D12GraphicsCommandList* CommandList() const { return _cmdList.Get(); }
    ID3D12CommandQueue* CommandQueue() const { return _cmdQueue.Get(); }
    ID3D12CommandAllocator* CommandAllocator() const { return _cmdAllocator.Get(); }
    ID3D12Fence* Fence() const { return _fence.Get(); }
    UINT64& FenceVal() { return _fenceVal; }
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> GetHeapForImgui() const { return _heapForImgui; }
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> PeraSRVHeap() const { return _peraSRVHeap; }

    // 関数の宣言のみを記述
    bool Init(HWND hwnd, int window_width, int window_height);
    void BeginDraw();
    void PreDrawToPera();
    void PostDrawToPera();
    void EndDraw();
    void WaitForGPU();

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(size_t sizeInBytes, const void* data, size_t dataSize = 0);
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureFromData(
        UINT64 width,
        UINT height,
        DXGI_FORMAT format,
        const void* pixels,
        size_t rowPitch,
        size_t slicePitch
    );
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateSolidColorTexture(
        uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255
    );
};