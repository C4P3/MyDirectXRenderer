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
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeaps;

    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> _backBuffers;

    Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
    UINT64 _fenceVal = 0;

    D3D12_VIEWPORT _viewport = {};
    D3D12_RECT _scissorRect = {};

public:
    ID3D12Device* Device() const { return _dev.Get(); }
    ID3D12GraphicsCommandList* CommandList() const { return _cmdList.Get(); }
    ID3D12CommandQueue* CommandQueue() const { return _cmdQueue.Get(); }
    ID3D12CommandAllocator* CommandAllocator() const { return _cmdAllocator.Get(); }
    ID3D12Fence* Fence() const { return _fence.Get(); }
    UINT64& FenceVal() { return _fenceVal; }

    // 関数の宣言のみを記述
    bool Init(HWND hwnd, int window_width, int window_height);
    void BeginDraw();
    void EndDraw();
    void WaitForGPU();

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(size_t sizeInBytes, const void* data, size_t dataSize = 0);

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureFromFile(
        const wchar_t* filePath
    );
};