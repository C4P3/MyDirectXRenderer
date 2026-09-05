// TexturePool — フレームを越えて物理リソースを持ち回すプール
//
// RenderGraph（論理層）が計算したライフタイムに基づいて、ここから物理リソースを引く。
// RHI 依存はすべて IResourceAllocator の向こう側に隔離してあるので、
// プールの方針（キー・追い出し・遅延解放・予算）は Mac 上でテストできる。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RenderGraph.h"

namespace rg {

constexpr uint32_t kInvalidPoolEntry = 0xffffffffu;

// --- RHI の継ぎ目 その 2 ---------------------------------------------------
// Windows 実装は CreateCommittedResource / CreatePlacedResource を呼び、
// ディスクリプタ（RTV/DSV/SRV）の確保もここで面倒を見ることになる。
class IResourceAllocator {
public:
    virtual ~IResourceAllocator() = default;

    // usageFlags は Compile() のステップ 1 で導出したもの。
    // D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET などに翻訳される。
    //
    // initialState は「このリソースが最初に必要とする状態」。DX12 では生成時に
    // 状態を決めなければならないので、グラフ側の想定と食い違わないよう外から渡す。
    // これのおかげで 1 フレーム目に初期バリアが要らない。
    virtual uint32_t Allocate(const std::string& name, const TextureDesc& desc,
                              uint8_t usageFlags, size_t sizeBytes,
                              State initialState) = 0;
    virtual void     Release(uint32_t physicalId)                   = 0;
};

// Mac 用のダミー。連番を振って記録するだけ。
class FakeResourceAllocator : public IResourceAllocator {
public:
    struct Record {
        std::string name;
        size_t      sizeBytes = 0;
        uint8_t     usageFlags = 0;
        State       initialState = State::Undefined;
        bool        alive = true;
    };

    uint32_t Allocate(const std::string& name, const TextureDesc&, uint8_t usageFlags,
                      size_t sizeBytes, State initialState) override {
        records.push_back(Record{ name, sizeBytes, usageFlags, initialState, true });
        ++allocateCount;
        return static_cast<uint32_t>(records.size() - 1);
    }
    void Release(uint32_t physicalId) override {
        records[physicalId].alive = false;
        ++releaseCount;
    }

    size_t AliveCount() const {
        size_t n = 0;
        for (const auto& r : records) if (r.alive) ++n;
        return n;
    }

    std::vector<Record> records;
    int allocateCount = 0;
    int releaseCount  = 0;
};

// TextureDesc から必要バイト数を概算する。
// ★ Windows では GetResourceAllocationInfo() を使うこと。
//    アラインメントとタイリングで実際の消費量は変わるので、この概算は Mac 検証用。
size_t EstimateSizeBytes(const TextureDesc& desc);
uint64_t HashDesc(const TextureDesc& desc);

// --- プール本体 -------------------------------------------------------------
class TexturePool {
public:
    // 何フレーム要求されなかったら追い出すか（スワップチェーン枚数 + 余裕）
    static constexpr uint64_t kEvictAfterFrames = 3;

    explicit TexturePool(IResourceAllocator& allocator) : _allocator(allocator) {}

    // 0 = 無制限。ロードマップ B の「GPU メモリ上限」がここ。
    void   SetBudgetBytes(size_t bytes) { _budgetBytes = bytes; }
    size_t UsedBytes() const { return _usedBytes; }
    size_t BudgetBytes() const { return _budgetBytes; }

    void BeginFrame();
    // 予算超過なら kInvalidPoolEntry を返す（事前に弾く）。
    // initialState は新規確保のときだけ使う（既存を引くならその状態が生きている）。
    uint32_t Acquire(const std::string& name, const TextureDesc& desc, uint8_t usageFlags,
                     State initialState = State::Undefined);

    State StateOf(uint32_t entry) const { return _entries[entry].state; }
    void  SetState(uint32_t entry, State s) { _entries[entry].state = s; }
    uint32_t PhysicalId(uint32_t entry) const { return _entries[entry].physicalId; }
    bool     WasFreshlyAllocated(uint32_t entry) const { return _entries[entry].freshlyAllocated; }

    // このフレームで使われなかった古いエントリを保留キューへ送る。
    // fenceValue = このフレームの完了を示すフェンス値。
    void EndFrame(uint64_t fenceValue);
    // GPU が completedFence まで進んだので、そこまでの保留分を実際に破棄する。
    void Reclaim(uint64_t completedFence);

    // --- テスト用 ---
    size_t LiveEntryCount() const { return _entries.size(); }
    size_t PendingReleaseCount() const { return _pending.size(); }
    uint64_t CurrentFrame() const { return _frame; }
    bool   HasEntryFor(const std::string& name) const;

private:
    struct Entry {
        std::string name;
        uint64_t    descHash = 0;
        TextureDesc desc;
        uint32_t    physicalId = 0;
        size_t      sizeBytes  = 0;
        State       state = State::Undefined;
        uint64_t    lastUsedFrame = 0;
        bool        inUseThisFrame   = false;
        bool        freshlyAllocated = false;
    };
    struct Pending {
        uint32_t physicalId = 0;
        size_t   sizeBytes  = 0;
        uint64_t fenceValue = 0;
    };

    IResourceAllocator&  _allocator;
    std::vector<Entry>   _entries;
    std::vector<Pending> _pending;
    uint64_t             _frame       = 0;
    size_t               _budgetBytes = 0;
    // 保留中（まだ GPU が触っている可能性がある）分も含む常駐量
    size_t               _usedBytes   = 0;
};

}  // namespace rg
