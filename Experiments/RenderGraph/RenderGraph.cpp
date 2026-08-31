#include "RenderGraph.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

#include "CommandContext.h"
#include "TexturePool.h"

namespace rg {

const char* ToString(State s) {
    switch (s) {
        case State::Undefined:           return "Undefined";
        case State::Present:             return "Present";
        case State::RenderTarget:        return "RenderTarget";
        case State::PixelShaderResource: return "PixelShaderResource";
        case State::DepthWrite:          return "DepthWrite";
    }
    return "?";
}

State Required(AccessKind k) {
    switch (k) {
        case AccessKind::SampledRead:     return State::PixelShaderResource;
        case AccessKind::ColorAttachment: return State::RenderTarget;
        case AccessKind::DepthAttachment: return State::DepthWrite;
    }
    return State::Undefined;
}

// --- リソースの登録 ---------------------------------------------------------

TextureHandle RenderGraph::Create(const char* name, const TextureDesc& desc) {
    VirtualResource r;
    r.name     = name;
    r.desc     = desc;
    r.imported = false;
    // stateAtFrameStart は Compile() のステップ 6 でプールから引く。
    r.producer.push_back(kNoPass);  // version 0 は誰も produce していない

    _resources.push_back(std::move(r));
    return TextureHandle{ static_cast<uint16_t>(_resources.size() - 1), 0 };
}

TextureHandle RenderGraph::Import(const char* name, const TextureDesc& desc,
                                  State initial, State requiredFinal) {
    VirtualResource r;
    r.name               = name;
    r.desc               = desc;
    r.imported           = true;
    r.stateAtFrameStart  = initial;
    r.requiredFinalState = requiredFinal;
    r.producer.push_back(kNoPass);

    _resources.push_back(std::move(r));
    return TextureHandle{ static_cast<uint16_t>(_resources.size() - 1), 0 };
}

// --- Builder ---------------------------------------------------------------

TextureHandle RenderGraph::Builder::Create(const char* name, const TextureDesc& desc) {
    return _g.Create(name, desc);
}

TextureHandle RenderGraph::Builder::SampledRead(TextureHandle h) {
    assert(h.IsValid());
    _accesses.push_back(Access{ h.index, h.version, AccessKind::SampledRead, 0, LoadOp::Load });
    return h;  // 読みはバージョンを進めない
}

TextureHandle RenderGraph::Builder::Write(TextureHandle h, AccessKind kind,
                                          uint8_t slot, LoadOp op) {
    assert(h.IsValid());
    auto& r = _g._resources[h.index];

    // 古いバージョンへの write は「ハンドルの繋ぎ忘れ」バグ。
    // Write の戻り値を下流に渡していれば必ず最新バージョンになる。
    assert(h.version + 1 == r.producer.size() &&
           "stale handle: Write() の戻り値を使っていない可能性がある");

    _accesses.push_back(Access{ h.index, h.version, kind, slot, op });
    r.producer.push_back(_pass);
    return TextureHandle{ h.index, static_cast<uint16_t>(h.version + 1) };
}

TextureHandle RenderGraph::Builder::SetRenderAttachment(TextureHandle h, uint32_t slot, LoadOp op) {
    return Write(h, AccessKind::ColorAttachment, static_cast<uint8_t>(slot), op);
}

TextureHandle RenderGraph::Builder::SetDepthAttachment(TextureHandle h, LoadOp op) {
    return Write(h, AccessKind::DepthAttachment, 0, op);
}

// --- Compile ---------------------------------------------------------------

State RenderGraph::FirstRequiredState(uint16_t resource) const {
    for (uint16_t p : _order)
        for (const auto& a : _passes[p].accesses)
            if (a.resource == resource) return Required(a.kind);
    return State::Undefined;
}

bool RenderGraph::Compile(TexturePool& pool) {
    const uint16_t passCount = static_cast<uint16_t>(_passes.size());

    _edges.clear();
    _order.clear();
    _barriersBeforePass.clear();
    _endBarriers.clear();
    _allocationFailures.clear();

    // === 1. 用途フラグの集計 ===============================================
    // TextureDesc に書かせなかった RT/DSV/SRV の区別が、ここで宣言から決まる。
    for (const auto& p : _passes) {
        for (const auto& a : p.accesses) {
            auto& r = _resources[a.resource];
            switch (a.kind) {
                case AccessKind::SampledRead:     r.usageFlags |= Usage::ShaderResource; break;
                case AccessKind::ColorAttachment: r.usageFlags |= Usage::RenderTarget;   break;
                case AccessKind::DepthAttachment: r.usageFlags |= Usage::DepthStencil;   break;
            }
        }
    }

    // === 2. 辺の導出 =======================================================
    // バージョニングにより write-after-write が自動的に read-after-write になる。
    for (uint16_t p = 0; p < passCount; ++p) {
        for (const auto& a : _passes[p].accesses) {
            const uint16_t prod = _resources[a.resource].producer[a.version];
            if (prod == kNoPass || prod == p) continue;
            _edges.emplace_back(prod, p);
            // ハンドルを繋ぐ API なので、辺は必ず「先に宣言されたパス → 後」を向く。
            // つまり宣言順がそのまま妥当なトポロジカル順序（→ ソート不要）。
            assert(prod < p && "宣言順がトポロジカル順序になっていない");
        }
    }

    // === 3. カリング（参照カウント） ========================================
    for (auto& p : _passes) { p.refCount = 0; p.culled = false; }
    for (auto& r : _resources) {
        r.refCount  = 0;
        r.firstUse  = kNoPass;
        r.lastUse   = kNoPass;
    }

    for (auto& p : _passes) {
        for (const auto& a : p.accesses) {
            if (IsWrite(a.kind)) p.refCount++;
            else                 _resources[a.resource].refCount++;
        }
    }
    // カリングの根：外部に出ていくリソース（= requiredFinalState を持つもの）
    for (auto& r : _resources)
        if (r.requiredFinalState != State::Undefined) r.refCount++;

    std::vector<uint16_t> stack;
    for (uint16_t i = 0; i < _resources.size(); ++i)
        if (_resources[i].refCount == 0) stack.push_back(i);

    while (!stack.empty()) {
        const uint16_t ri = stack.back();
        stack.pop_back();

        for (size_t v = 1; v < _resources[ri].producer.size(); ++v) {
            const uint16_t p = _resources[ri].producer[v];
            if (p == kNoPass || _passes[p].culled) continue;
            if (--_passes[p].refCount != 0) continue;

            _passes[p].culled = true;  // 出力を誰も読まない → 不要
            for (const auto& a : _passes[p].accesses) {
                if (IsWrite(a.kind)) continue;
                if (--_resources[a.resource].refCount == 0) stack.push_back(a.resource);
            }
        }
    }

    // === 4. 実行順 =========================================================
    // ソート不要（2 の assert 参照）。カリングされていないパスを宣言順に並べるだけ。
    for (uint16_t p = 0; p < passCount; ++p)
        if (!_passes[p].culled) _order.push_back(p);

    // === 5. ライフタイム ===================================================
    // 添字は「実行順」であることに注意（宣言順ではない）。
    for (uint16_t i = 0; i < _order.size(); ++i) {
        for (const auto& a : _passes[_order[i]].accesses) {
            auto& r = _resources[a.resource];
            if (r.firstUse == kNoPass) r.firstUse = i;
            r.lastUse = i;
        }
    }

    // === 6. 物理リソースの取得（プール） ====================================
    // カリングされたリソース（firstUse == kNoPass）は確保しない。
    // = Unity の「フレームが使用しないリソースの割り当てを回避する」に相当。
    bool ok = true;
    for (auto& r : _resources) {
        r.poolEntry = kInvalidPoolEntry;
        if (r.imported || r.firstUse == kNoPass) continue;

        r.poolEntry = pool.Acquire(r.name, r.desc, r.usageFlags);
        if (r.poolEntry == kInvalidPoolEntry) {
            _allocationFailures.push_back(r.name);  // 予算超過。事前に弾かれた
            ok = false;
            continue;
        }
        // 状態は物理リソースが持つ。新規確保なら Undefined。
        r.stateAtFrameStart = pool.StateOf(r.poolEntry);
    }
    if (!ok) return false;

    // === 7. バリア導出 =====================================================
    _barriersBeforePass.assign(_order.size(), {});

    for (uint16_t ri = 0; ri < _resources.size(); ++ri) {
        auto& r = _resources[ri];
        if (r.firstUse == kNoPass) continue;  // 誰も使わない

        State cur = r.stateAtFrameStart;
        if (cur == State::Undefined) {
            // 初回フレーム：最初に必要な状態でリソースを作る → 初期バリアが要らない
            cur = FirstRequiredState(ri);
            r.stateAtFrameStart = cur;
        }

        for (uint16_t i = 0; i < _order.size(); ++i) {
            for (const auto& a : _passes[_order[i]].accesses) {
                if (a.resource != ri) continue;
                const State need = Required(a.kind);
                if (need == cur) continue;
                _barriersBeforePass[i].push_back(Barrier{ ri, cur, need });
                cur = need;
            }
        }

        if (r.requiredFinalState != State::Undefined && cur != r.requiredFinalState) {
            _endBarriers.push_back(Barrier{ ri, cur, r.requiredFinalState });
            cur = r.requiredFinalState;
        }

        r.stateAtFrameEnd = cur;
        // インポート物は毎フレーム外から初期状態が決まるので書き戻さない。
        if (!r.imported) pool.SetState(r.poolEntry, cur);
    }

    return true;
}

// --- Execute ---------------------------------------------------------------

void RenderGraph::Execute(CommandContext& ctx) {
    for (uint16_t i = 0; i < _order.size(); ++i) {
        for (const auto& b : _barriersBeforePass[i])
            ctx.Transition(_resources[b.resource].name, b.from, b.to);

        auto& pass = _passes[_order[i]];
        ctx.BeginPass(pass.name);
        pass.execute(ctx);
        ctx.EndPass();
    }
    for (const auto& b : _endBarriers)
        ctx.Transition(_resources[b.resource].name, b.from, b.to);
}

void RenderGraph::Clear() {
    _passes.clear();
    _resources.clear();
    _edges.clear();
    _order.clear();
    _barriersBeforePass.clear();
    _endBarriers.clear();
    _allocationFailures.clear();
    // 物理リソースは TexturePool 側に残る
}

// --- 覗き窓 / デバッグ出力 --------------------------------------------------

const VirtualResource* RenderGraph::FindResource(const std::string& name) const {
    for (const auto& r : _resources)
        if (r.name == name) return &r;
    return nullptr;
}

std::vector<std::string> RenderGraph::OrderedPassNames() const {
    std::vector<std::string> out;
    for (uint16_t p : _order) out.push_back(_passes[p].name);
    return out;
}

bool RenderGraph::IsCulled(const std::string& passName) const {
    for (const auto& p : _passes)
        if (p.name == passName) return p.culled;
    return false;
}

size_t RenderGraph::BarrierCount() const {
    size_t n = _endBarriers.size();
    for (const auto& v : _barriersBeforePass) n += v.size();
    return n;
}

static std::string UsageString(uint8_t flags) {
    std::string s;
    if (flags & Usage::RenderTarget)   s += "RT ";
    if (flags & Usage::DepthStencil)   s += "DS ";
    if (flags & Usage::ShaderResource) s += "SRV ";
    if (s.empty()) s = "-";
    return s;
}

void RenderGraph::Dump() const {
    std::printf("--- passes (%zu declared, %zu executed) ---\n", _passes.size(), _order.size());
    for (const auto& p : _passes)
        std::printf("  %-20s %s\n", p.name.c_str(), p.culled ? "[culled]" : "");

    std::printf("--- edges ---\n");
    for (const auto& e : _edges)
        std::printf("  %s -> %s\n", _passes[e.first].name.c_str(), _passes[e.second].name.c_str());

    std::printf("--- resources ---\n");
    for (const auto& r : _resources) {
        std::printf("  %-12s %-9s usage=%-10s lifetime=[%d..%d] %s -> %s\n",
                    r.name.c_str(),
                    r.imported ? "imported" : "owned",
                    UsageString(r.usageFlags).c_str(),
                    static_cast<int>(r.firstUse == kNoPass ? -1 : r.firstUse),
                    static_cast<int>(r.lastUse == kNoPass ? -1 : r.lastUse),
                    ToString(r.stateAtFrameStart), ToString(r.stateAtFrameEnd));
    }
    std::printf("--- barriers: %zu ---\n", BarrierCount());
}

}  // namespace rg
