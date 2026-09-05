// Dx12ResourceAllocator — RHI の継ぎ目その 2（IResourceAllocator）の DX12 実装
//
// physicalId から ID3D12Resource* とディスクリプタを引ける唯一の場所。
// RenderGraph 本体も TexturePool も実体を知らず、この id だけを持ち回す。
//
// 扱うリソースは 2 種類:
//   - owned    : TexturePool 経由で Allocate() したもの。ディスクリプタもここで確保する
//   - external : グラフの外で作られたものを預かったもの（スワップチェーンのバックバッファ）
//
// このファイルは UTF-8 (BOM 付き)。
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "Frontend/TexturePool.h"

// rg::State を D3D12 の状態に翻訳する。バックエンド共通。
D3D12_RESOURCE_STATES ToD3D12(rg::State s);

class Dx12ResourceAllocator : public rg::IResourceAllocator {
public:
    // ディスクリプタヒープの容量。足りなくなったら assert で気付く。
    static constexpr UINT kMaxRtv = 16;
    static constexpr UINT kMaxDsv = 8;
    static constexpr UINT kMaxSrv = 32;

    explicit Dx12ResourceAllocator(ID3D12Device* dev);

    // グラフの外で作られた実体を預かり、id を発番する。
    // 実体が生きている限り id は有効なので、スワップチェーンのバッファは
    // 初期化時に全枚数を登録しておき、毎フレーム該当する id で Import すればよい。
    uint32_t RegisterExternalRenderTarget(ID3D12Resource* res, D3D12_CPU_DESCRIPTOR_HANDLE rtv);

    // physicalId → 実体 / ディスクリプタ。未登録なら nullptr、または ptr == 0。
    ID3D12Resource*             Resolve(uint32_t physicalId) const;
    D3D12_CPU_DESCRIPTOR_HANDLE RtvOf(uint32_t physicalId) const;
    D3D12_CPU_DESCRIPTOR_HANDLE DsvOf(uint32_t physicalId) const;

    // SampledRead したリソースをシェーダに渡すための GPU ハンドル。
    // 使う前に SrvHeap() を SetDescriptorHeaps すること。
    D3D12_GPU_DESCRIPTOR_HANDLE SrvOf(uint32_t physicalId) const;
    ID3D12DescriptorHeap*       SrvHeap() const { return _srvHeap.Get(); }

    // --- rg::IResourceAllocator ---
    uint32_t Allocate(const std::string& name, const rg::TextureDesc& desc,
                      uint8_t usageFlags, size_t sizeBytes, rg::State initialState) override;
    void     Release(uint32_t physicalId) override;

private:
    static constexpr UINT kNoSlot = 0xffffffffu;

    struct Entry {
        // Allocate() したものだけ所有する。external は参照のみ。
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;
        ID3D12Resource*             ptr      = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv      = {};
        D3D12_CPU_DESCRIPTOR_HANDLE dsv      = {};
        D3D12_GPU_DESCRIPTOR_HANDLE srv      = {};
        // 解放時に返すためのスロット番号（external は確保していないので kNoSlot）
        UINT rtvSlot = kNoSlot;
        UINT dsvSlot = kNoSlot;
        UINT srvSlot = kNoSlot;
        bool external = false;
        bool alive    = false;
    };

    // 追記式 + 空きリストの、ごく単純なディスクリプタ割り当て
    struct HeapAlloc {
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        UINT              increment = 0;
        UINT              capacity  = 0;
        UINT              next      = 0;
        std::vector<UINT> freeList;

        UINT Alloc();
        void Free(UINT slot);
        D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT slot) const;
        D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT slot) const;
    };

    uint32_t Add(const Entry& e);
    void     CreateHeap(HeapAlloc& out, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity,
                        bool shaderVisible);

    ID3D12Device*         _dev = nullptr;
    std::vector<Entry>    _entries;
    std::vector<uint32_t> _freeList;  // Release() で空いた添字の再利用

    HeapAlloc _rtv;
    HeapAlloc _dsv;
    HeapAlloc _srv;
    // SrvHeap() で返す用（HeapAlloc の中身と同じもの）
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _srvHeap;
};
