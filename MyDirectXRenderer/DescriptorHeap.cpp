#include "DescriptorHeap.h"

#include <assert.h>

bool DescriptorHeap::Init(ID3D12Device* dev, UINT capacity) {
    assert(dev != nullptr);
    assert(capacity > 0);

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = capacity;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask       = 0;

    HRESULT hr = dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(_heap.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return false;

    _increment = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    _capacity  = capacity;
    _next      = 0;
    _freeList.clear();
    return true;
}

DescriptorHeap::Range DescriptorHeap::Alloc(UINT count) {
    assert(count > 0);

    // 1 個だけなら解放済みスロットを使い回せる。
    if (count == 1 && !_freeList.empty()) {
        const UINT slot = _freeList.back();
        _freeList.pop_back();
        return Range{ slot, 1 };
    }

    if (_next + count > _capacity) {
        assert(false && "ディスクリプタヒープが足りない（容量を増やすこと）");
        return Range{};
    }

    const UINT first = _next;
    _next += count;
    return Range{ first, count };
}

void DescriptorHeap::Free(Range range) {
    if (!range.Valid()) return;
    for (UINT i = 0; i < range.count; ++i) _freeList.push_back(range.first + i);
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::Cpu(UINT slot) const {
    assert(slot < _capacity);
    auto h = _heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * _increment;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::Gpu(UINT slot) const {
    assert(slot < _capacity);
    auto h = _heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * _increment;
    return h;
}
