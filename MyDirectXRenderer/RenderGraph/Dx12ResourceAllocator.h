// Dx12ResourceAllocator — RHI の継ぎ目その 2（IResourceAllocator）の DX12 実装
//
// physicalId から ID3D12Resource* を引ける唯一の場所。
// RenderGraph 本体も TexturePool も実体を知らず、この id だけを持ち回す。
//
// 扱うリソースは 2 種類:
//   - owned    : TexturePool 経由で Allocate() したもの（段階 3 で実装）
//   - external : グラフの外で作られたものを預かったもの（スワップチェーンのバックバッファなど）
//
// このファイルは UTF-8 (BOM 付き)。
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "Frontend/TexturePool.h"

class Dx12ResourceAllocator : public rg::IResourceAllocator {
public:
    explicit Dx12ResourceAllocator(ID3D12Device* dev) : _dev(dev) {}

    // グラフの外で作られた実体を預かり、id を発番する。
    // 実体が生きている限り id は有効なので、スワップチェーンのバッファは
    // 初期化時に全枚数を登録しておき、毎フレーム該当する id で Import すればよい。
    uint32_t RegisterExternal(ID3D12Resource* res);

    // physicalId → 実体。未登録なら nullptr。
    ID3D12Resource* Resolve(uint32_t physicalId) const;

    // --- rg::IResourceAllocator ---
    uint32_t Allocate(const std::string& name, const rg::TextureDesc& desc,
                      uint8_t usageFlags, size_t sizeBytes) override;
    void     Release(uint32_t physicalId) override;

private:
    struct Entry {
        // Allocate() したものだけ所有する。external は参照のみ。
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;
        ID3D12Resource* ptr      = nullptr;
        bool            external = false;
        bool            alive    = false;
    };

    ID3D12Device*         _dev = nullptr;
    std::vector<Entry>    _entries;
    std::vector<uint32_t> _freeList;  // Release() で空いた添字の再利用
};
