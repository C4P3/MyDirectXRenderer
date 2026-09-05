#include "Dx12ResourceAllocator.h"

#include <assert.h>

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

uint32_t Dx12ResourceAllocator::RegisterExternalDepth(
    ID3D12Resource* res, D3D12_CPU_DESCRIPTOR_HANDLE dsv) {
    assert(res != nullptr);
    Entry e;
    e.ptr      = res;
    e.dsv      = dsv;
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

ID3D12Resource* Dx12ResourceAllocator::Resolve(uint32_t physicalId) const {
    if (physicalId >= _entries.size()) return nullptr;
    if (!_entries[physicalId].alive) return nullptr;
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

uint32_t Dx12ResourceAllocator::Allocate(const std::string&, const rg::TextureDesc&,
                                         uint8_t, size_t) {
    // 段階 3（TexturePool の導入）で実装する。
    // CreateCommittedResource に加えて RTV / DSV / SRV の確保もここで面倒を見ることになる。
    // 段階 1 では全リソースを Import しているので、ここには来ない。
    assert(false && "Dx12ResourceAllocator::Allocate is not implemented yet (see step 3)");
    return rg::kInvalidPhysicalId;
}

void Dx12ResourceAllocator::Release(uint32_t physicalId) {
    if (physicalId >= _entries.size()) return;

    Entry& e = _entries[physicalId];
    e.owned.Reset();
    e.ptr   = nullptr;
    e.rtv   = {};
    e.dsv   = {};
    e.alive = false;
    _freeList.push_back(physicalId);
}
