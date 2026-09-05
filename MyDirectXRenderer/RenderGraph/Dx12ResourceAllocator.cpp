#include "Dx12ResourceAllocator.h"

#include <assert.h>
#include <string.h>

#include "../d3dx12.h"

D3D12_RESOURCE_STATES ToD3D12(rg::State s) {
    switch (s) {
    case rg::State::Present:             return D3D12_RESOURCE_STATE_PRESENT;
    case rg::State::RenderTarget:        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case rg::State::PixelShaderResource: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case rg::State::DepthWrite:          return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case rg::State::Undefined:
    default:
        // Undefined は「指定なし」の番兵。バリアの端点にも生成時の状態にも来ない。
        assert(false && "rg::State::Undefined has no D3D12 counterpart");
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

namespace {

// リソース自体のフォーマット
DXGI_FORMAT ResourceFormat(rg::Format f) {
    switch (f) {
    case rg::Format::D32_Float:   return DXGI_FORMAT_D32_FLOAT;
    case rg::Format::RGBA8_UNorm:
    default:                      return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

// ビュー（RTV / SRV / DSV）のフォーマット。
// 現状の Dx12Wrapper に合わせて、カラーは sRGB のビューを張る。
DXGI_FORMAT ViewFormat(rg::Format f) {
    switch (f) {
    case rg::Format::D32_Float:   return DXGI_FORMAT_D32_FLOAT;
    case rg::Format::RGBA8_UNorm:
    default:                      return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
}

}  // namespace

// --- HeapAlloc -------------------------------------------------------------

UINT Dx12ResourceAllocator::HeapAlloc::Alloc() {
    if (!freeList.empty()) {
        const UINT slot = freeList.back();
        freeList.pop_back();
        return slot;
    }
    assert(next < capacity && "ディスクリプタヒープが足りない（容量を増やすこと）");
    return next++;
}

void Dx12ResourceAllocator::HeapAlloc::Free(UINT slot) { freeList.push_back(slot); }

D3D12_CPU_DESCRIPTOR_HANDLE Dx12ResourceAllocator::HeapAlloc::Cpu(UINT slot) const {
    auto h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * increment;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE Dx12ResourceAllocator::HeapAlloc::Gpu(UINT slot) const {
    auto h = heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * increment;
    return h;
}

// --- 初期化 -----------------------------------------------------------------

void Dx12ResourceAllocator::CreateHeap(HeapAlloc& out, D3D12_DESCRIPTOR_HEAP_TYPE type,
                                       UINT capacity, bool shaderVisible) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = type;
    desc.NumDescriptors = capacity;
    desc.Flags          = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                        : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask       = 0;

    HRESULT hr = _dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(out.heap.ReleaseAndGetAddressOf()));
    assert(SUCCEEDED(hr));
    (void)hr;

    out.increment = _dev->GetDescriptorHandleIncrementSize(type);
    out.capacity  = capacity;
    out.next      = 0;
}

Dx12ResourceAllocator::Dx12ResourceAllocator(ID3D12Device* dev) : _dev(dev) {
    assert(dev != nullptr);
    CreateHeap(_rtv, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,         kMaxRtv, false);
    CreateHeap(_dsv, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,         kMaxDsv, false);
    CreateHeap(_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxSrv, true);
    _srvHeap = _srv.heap;
}

// --- 外部所有リソースの登録 -------------------------------------------------

uint32_t Dx12ResourceAllocator::RegisterExternalRenderTarget(
    ID3D12Resource* res, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    assert(res != nullptr);
    Entry e;
    e.ptr      = res;
    e.rtv      = rtv;
    e.external = true;
    e.alive    = true;
    return Add(e);
}

uint32_t Dx12ResourceAllocator::Add(const Entry& e) {
    if (!_freeList.empty()) {
        const uint32_t id = _freeList.back();
        _freeList.pop_back();
        _entries[id] = e;
        return id;
    }
    _entries.push_back(e);
    return static_cast<uint32_t>(_entries.size() - 1);
}

// --- 参照 -------------------------------------------------------------------

ID3D12Resource* Dx12ResourceAllocator::Resolve(uint32_t physicalId) const {
    if (physicalId >= _entries.size() || !_entries[physicalId].alive) return nullptr;
    return _entries[physicalId].ptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12ResourceAllocator::RtvOf(uint32_t physicalId) const {
    if (physicalId >= _entries.size() || !_entries[physicalId].alive) return {};
    return _entries[physicalId].rtv;
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12ResourceAllocator::DsvOf(uint32_t physicalId) const {
    if (physicalId >= _entries.size() || !_entries[physicalId].alive) return {};
    return _entries[physicalId].dsv;
}

D3D12_GPU_DESCRIPTOR_HANDLE Dx12ResourceAllocator::SrvOf(uint32_t physicalId) const {
    if (physicalId >= _entries.size() || !_entries[physicalId].alive) return {};
    return _entries[physicalId].srv;
}

// --- 確保と解放 -------------------------------------------------------------

uint32_t Dx12ResourceAllocator::Allocate(const std::string&, const rg::TextureDesc& desc,
                                         uint8_t usageFlags, size_t, rg::State initialState) {
    const bool isRT  = (usageFlags & rg::Usage::RenderTarget) != 0;
    const bool isDS  = (usageFlags & rg::Usage::DepthStencil) != 0;
    const bool isSRV = (usageFlags & rg::Usage::ShaderResource) != 0;

    // 深度を SRV で読むには TYPELESS で作ってビューごとにフォーマットを変える必要がある。
    // シャドウマップを入れるときに対応する。
    assert(!(isDS && isSRV) && "深度を SRV で読むのは未対応");

    auto resDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        ResourceFormat(desc.format), desc.width, desc.height, 1, 1);
    if (isRT) resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (isDS) {
        resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (!isSRV) resDesc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }

    // クリア値は TextureDesc が持っている。LoadOp::Clear のときこの値でクリアされる。
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = ViewFormat(desc.format);
    if (isDS) {
        clearValue.DepthStencil.Depth   = desc.clearDepth;
        clearValue.DepthStencil.Stencil = 0;
    } else {
        memcpy(clearValue.Color, desc.clearColor, sizeof(clearValue.Color));
    }

    Entry e;
    auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = _dev->CreateCommittedResource(
        &heapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
        ToD3D12(initialState),                      // グラフが決めた「最初に必要な状態」
        (isRT || isDS) ? &clearValue : nullptr,
        IID_PPV_ARGS(e.owned.ReleaseAndGetAddressOf()));
    assert(SUCCEEDED(hr));
    if (FAILED(hr)) return rg::kInvalidPhysicalId;

    e.ptr   = e.owned.Get();
    e.alive = true;

    if (isRT) {
        e.rtvSlot = _rtv.Alloc();
        e.rtv     = _rtv.Cpu(e.rtvSlot);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Format        = ViewFormat(desc.format);
        _dev->CreateRenderTargetView(e.ptr, &rtvDesc, e.rtv);
    }
    if (isDS) {
        e.dsvSlot = _dsv.Alloc();
        e.dsv     = _dsv.Cpu(e.dsvSlot);

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Format        = ViewFormat(desc.format);
        dsvDesc.Flags         = D3D12_DSV_FLAG_NONE;
        _dev->CreateDepthStencilView(e.ptr, &dsvDesc, e.dsv);
    }
    if (isSRV) {
        e.srvSlot = _srv.Alloc();
        e.srv     = _srv.Gpu(e.srvSlot);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Format                  = ViewFormat(desc.format);
        srvDesc.Texture2D.MipLevels     = 1;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        _dev->CreateShaderResourceView(e.ptr, &srvDesc, _srv.Cpu(e.srvSlot));
    }

    return Add(e);
}

void Dx12ResourceAllocator::Release(uint32_t physicalId) {
    if (physicalId >= _entries.size()) return;

    Entry& e = _entries[physicalId];
    if (e.rtvSlot != kNoSlot) _rtv.Free(e.rtvSlot);
    if (e.dsvSlot != kNoSlot) _dsv.Free(e.dsvSlot);
    if (e.srvSlot != kNoSlot) _srv.Free(e.srvSlot);

    e = Entry{};
    _freeList.push_back(physicalId);
}
