// RenderGraph 論理層（Frontend）のスケッチ
//
// D3D12 に一切依存しない。唯一の継ぎ目は CommandContext（CommandContext.h）で、
// これを差し替えることで Mac 上のダミーバックエンドでも DX12 でも動く想定。
//
// このファイルは Mac での実験用なので UTF-8。
// 本体プロジェクト（Shift-JIS）に取り込むときは変換が必要。
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rg {

constexpr uint16_t kInvalidIndex = 0xffff;
constexpr uint16_t kNoPass       = 0xffff;

// --- 1. ハンドル：整数 2 個。ポインタではない ------------------------------
struct TextureHandle {
    uint16_t index   = kInvalidIndex;  // 仮想リソース ID
    uint16_t version = 0;              // 何回目の write の結果か
    bool IsValid() const { return index != kInvalidIndex; }
};

// --- 2. リソース記述：用途（RT/DSV/SRV）は書かない -------------------------
// 用途は全パスの宣言を集計して Compile() が導出する。
enum class Format { RGBA8_UNorm, D32_Float };
enum class LoadOp { Load, Clear };

struct TextureDesc {
    uint32_t width  = 0;
    uint32_t height = 0;
    Format   format = Format::RGBA8_UNorm;
    float    clearColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
    float    clearDepth    = 1.0f;
};

namespace Usage {
constexpr uint8_t ShaderResource = 1 << 0;
constexpr uint8_t RenderTarget   = 1 << 1;
constexpr uint8_t DepthStencil   = 1 << 2;
}  // namespace Usage

// D3D12_RESOURCE_STATES の RHI 非依存版。必要な分だけ。
enum class State {
    Undefined,  // 「指定なし」の番兵も兼ねる
    Present,
    RenderTarget,
    PixelShaderResource,
    DepthWrite,
};
const char* ToString(State s);

// --- 3. アクセス記録 --------------------------------------------------------
enum class AccessKind : uint8_t { SampledRead, ColorAttachment, DepthAttachment };

struct Access {
    uint16_t   resource = kInvalidIndex;
    uint16_t   version  = 0;  // 「消費する」バージョン
    AccessKind kind     = AccessKind::SampledRead;
    uint8_t    slot     = 0;  // ColorAttachment のときだけ有効（MRT 用）
    LoadOp     load     = LoadOp::Load;
};

inline bool IsWrite(AccessKind k) { return k != AccessKind::SampledRead; }
State Required(AccessKind k);

// --- 4. 仮想リソース -------------------------------------------------------
struct VirtualResource {
    std::string name;
    TextureDesc desc;
    bool  imported = false;
    State stateAtFrameStart  = State::Undefined;
    State requiredFinalState = State::Undefined;  // backbuffer だけ Present

    // producer[v] = バージョン v を作ったパス。Create/Write のたびに builder が伸ばす。
    std::vector<uint16_t> producer;

    // Compile() の導出結果
    uint32_t poolEntry = 0xffffffffu;  // TexturePool のエントリ（フレーム内のみ有効）
    uint8_t usageFlags = 0;
    int32_t refCount   = 0;
    uint16_t firstUse  = kNoPass;  // 実行順でのインデックス
    uint16_t lastUse   = kNoPass;
    State   stateAtFrameEnd = State::Undefined;
};

struct Barrier {
    uint16_t resource = kInvalidIndex;
    State    from     = State::Undefined;
    State    to       = State::Undefined;
};

class CommandContext;
class TexturePool;

// --- 5. グラフ本体 ---------------------------------------------------------
class RenderGraph {
public:
    // 宣言の入口。パスごとに 1 つ渡される。
    class Builder {
    public:
        TextureHandle Create(const char* name, const TextureDesc& desc);

        // SRV として読む。ハンドルは変わらない。
        TextureHandle SampledRead(TextureHandle h);

        // アタッチメントスロットを明示して書く。★新しいバージョンを返す★
        [[nodiscard]] TextureHandle SetRenderAttachment(TextureHandle h, uint32_t slot, LoadOp op);
        [[nodiscard]] TextureHandle SetDepthAttachment(TextureHandle h, LoadOp op);

    private:
        friend class RenderGraph;
        Builder(RenderGraph& g, uint16_t pass) : _g(g), _pass(pass) {}
        TextureHandle Write(TextureHandle h, AccessKind kind, uint8_t slot, LoadOp op);

        RenderGraph&        _g;
        uint16_t            _pass;
        std::vector<Access> _accesses;
    };

    // グラフが所有するリソース
    TextureHandle Create(const char* name, const TextureDesc& desc);
    // 外部所有（スワップチェーンなど）
    TextureHandle Import(const char* name, const TextureDesc& desc,
                         State initial, State requiredFinal);

    // setup は毎フレーム走るが、GPU コマンドは一切積まない。
    // execute は Compile() が順序とバリアを決めたあとに呼ばれる。
    template <typename PassData, typename Setup, typename Execute>
    void AddPass(const char* name, Setup&& setup, Execute&& execute) {
        auto    data = std::make_shared<PassData>();
        Builder b(*this, static_cast<uint16_t>(_passes.size()));
        setup(b, *data);

        PassNode node;
        node.name     = name;
        node.accesses = std::move(b._accesses);
        node.execute  = [data, exec = std::forward<Execute>(execute)](CommandContext& ctx) {
            exec(*data, ctx);
        };
        _passes.push_back(std::move(node));
    }

    // 物理リソースの取得までやる。予算超過で確保できなければ false。
    // 失敗したリソース名は AllocationFailures() で取れる。
    [[nodiscard]] bool Compile(TexturePool& pool);
    void Execute(CommandContext& ctx);

    // 毎フレーム再宣言するためのリセット。リソースの状態だけは持ち越す。
    void Clear();

    void Dump() const;

    // --- テスト用の覗き窓 ---
    const VirtualResource& Resource(TextureHandle h) const { return _resources[h.index]; }
    const VirtualResource* FindResource(const std::string& name) const;
    const std::vector<uint16_t>& Order() const { return _order; }
    std::vector<std::string> OrderedPassNames() const;
    bool IsCulled(const std::string& passName) const;
    size_t BarrierCount() const;
    const std::vector<std::string>& AllocationFailures() const { return _allocationFailures; }

private:
    struct PassNode {
        std::string                          name;
        std::vector<Access>                  accesses;
        std::function<void(CommandContext&)> execute;
        int32_t                              refCount = 0;
        bool                                 culled   = false;
    };

    State FirstRequiredState(uint16_t resource) const;

    std::vector<PassNode>            _passes;
    std::vector<VirtualResource>     _resources;
    std::vector<std::pair<uint16_t, uint16_t>> _edges;  // (producer, consumer)
    std::vector<uint16_t>            _order;            // 実行順（カリング後）
    std::vector<std::vector<Barrier>> _barriersBeforePass;  // _order と同じ添字
    std::vector<Barrier>             _endBarriers;
    std::vector<std::string>         _allocationFailures;
};

}  // namespace rg
