// 論理層のテスト。GPU が無くても Compile()/Execute() の結果は全部検証できる。
#include <cstdio>
#include <string>
#include <vector>

#include "CommandContext.h"
#include "RenderGraph.h"
#include "TexturePool.h"

using namespace rg;

// --- ごく小さいテストハーネス ----------------------------------------------
static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static void CheckList(const char* what,
                      const std::vector<std::string>& actual,
                      const std::vector<std::string>& expected) {
    if (actual == expected) return;
    ++g_failures;
    std::printf("  FAIL %s\n    expected (%zu):\n", what, expected.size());
    for (const auto& s : expected) std::printf("      %s\n", s.c_str());
    std::printf("    actual (%zu):\n", actual.size());
    for (const auto& s : actual) std::printf("      %s\n", s.c_str());
}

// アロケータ + プール + グラフをまとめた入れ物
struct Pooled {
    FakeResourceAllocator alloc;
    TexturePool           pool{ alloc };
    RenderGraph           graph;
    uint64_t              fence = 0;

    bool Compile() {
        pool.BeginFrame();
        return graph.Compile(pool);
    }
    // フレーム終わり。GPU は 1 フレーム遅れて完了する想定。
    void EndFrame() {
        ++fence;
        pool.EndFrame(fence);
        if (fence >= 1) pool.Reclaim(fence - 1);
    }
};

// --- 現状の 4 パスを宣言する（Application::BuildGraph 相当） ----------------
// バックエンドが発番する physicalId の代役。実機ではスワップチェーンのバッファを
// アロケータに登録して得た id が入る。
constexpr uint32_t kFakeBackbufferId = 1000;

static void BuildCurrentGraph(RenderGraph& g) {
    const TextureDesc colorDesc{ 1280, 720, Format::RGBA8_UNorm, { 0.5f, 0.5f, 0.5f, 1.0f }, 1.0f };
    const TextureDesc depthDesc{ 1280, 720, Format::D32_Float, { 1, 1, 1, 1 }, 1.0f };

    TextureHandle pera1 = g.Create("pera1", colorDesc);
    TextureHandle pera2 = g.Create("pera2", colorDesc);
    TextureHandle depth = g.Create("depth", depthDesc);
    TextureHandle bb    = g.Import("backbuffer", colorDesc, kFakeBackbufferId, State::Present, State::Present);

    struct ScenePass { TextureHandle color, depth; };
    g.AddPass<ScenePass>(
        "3D",
        [&](RenderGraph::Builder& b, ScenePass& d) {
            d.color = pera1 = b.SetRenderAttachment(pera1, 0, LoadOp::Clear);
            d.depth = depth = b.SetDepthAttachment(depth, LoadOp::Clear);
        },
        [](const ScenePass&, CommandContext& ctx) {
            ctx.Draw("pmd");
            ctx.Draw("gregory");
        });

    struct BlurPass { TextureHandle src; };
    g.AddPass<BlurPass>(
        "BlurH",
        [&](RenderGraph::Builder& b, BlurPass& d) {
            d.src = b.SampledRead(pera1);
            pera2 = b.SetRenderAttachment(pera2, 0, LoadOp::Clear);
        },
        [](const BlurPass&, CommandContext& ctx) { ctx.Draw("pera-horizontal"); });

    g.AddPass<BlurPass>(
        "BlurV",
        [&](RenderGraph::Builder& b, BlurPass& d) {
            d.src = b.SampledRead(pera2);
            bb    = b.SetRenderAttachment(bb, 0, LoadOp::Clear);  // bb@v0 -> bb@v1
        },
        [](const BlurPass&, CommandContext& ctx) { ctx.Draw("pera-vertical"); });

    struct ImGuiPass { TextureHandle target; };
    g.AddPass<ImGuiPass>(
        "ImGui",
        [&](RenderGraph::Builder& b, ImGuiPass& d) {
            // bb@v1 -> bb@v2。これだけで BlurV -> ImGui の順序が確定する。
            d.target = bb = b.SetRenderAttachment(bb, 0, LoadOp::Load);
        },
        [](const ImGuiPass&, CommandContext& ctx) { ctx.Draw("imgui"); });
}

// === テスト: アタッチメント情報が execute の直前に渡る =====================
static void TestAttachments() {
    std::printf("[TestAttachments]\n");
    Pooled       h;
    RenderGraph& g = h.graph;

    BuildCurrentGraph(g);
    CHECK(h.Compile());
    LoggingCommandContext ctx;
    g.Execute(ctx);

    // 3D パス：カラー 1 枚 + 深度、どちらも Clear
    const auto* scene = ctx.AttachmentsOf("3D");
    CHECK(scene != nullptr);
    if (scene) {
        CHECK(scene->colors.size() == 1);
        CHECK(scene->colors[0].physicalId == g.FindResource("pera1")->physicalId);
        CHECK(scene->colors[0].load == LoadOp::Clear);
        CHECK(scene->colors[0].width == 1280 && scene->colors[0].height == 720);
        CHECK(scene->hasDepth);
        CHECK(scene->depth.physicalId == g.FindResource("depth")->physicalId);
        CHECK(scene->depth.load == LoadOp::Clear);
    }

    // BlurH パス：読みは含まれない。書き先の pera2 だけ
    const auto* blurH = ctx.AttachmentsOf("BlurH");
    CHECK(blurH != nullptr);
    if (blurH) {
        CHECK(blurH->colors.size() == 1);
        CHECK(blurH->colors[0].physicalId == g.FindResource("pera2")->physicalId);
        CHECK(!blurH->hasDepth);
    }

    // ImGui パス：バックバッファに Load で上書きする（クリアしない）
    const auto* imgui = ctx.AttachmentsOf("ImGui");
    CHECK(imgui != nullptr);
    if (imgui) {
        CHECK(imgui->colors.size() == 1);
        CHECK(imgui->colors[0].physicalId == kFakeBackbufferId);
        CHECK(imgui->colors[0].load == LoadOp::Load);
        CHECK(!imgui->hasDepth);
    }
}

// === テスト: 生成時の初期状態と、execute からのハンドル解決 =================
static void TestInitialStateAndResolve() {
    std::printf("[TestInitialStateAndResolve]\n");
    Pooled       h;
    RenderGraph& g = h.graph;

    const TextureDesc colorDesc{ 1280, 720, Format::RGBA8_UNorm, { 0.5f, 0.5f, 0.5f, 1.0f }, 1.0f };
    const TextureDesc depthDesc{ 1280, 720, Format::D32_Float, { 1, 1, 1, 1 }, 1.0f };

    TextureHandle color = g.Create("color", colorDesc);
    TextureHandle depth = g.Create("depth", depthDesc);
    TextureHandle bb    = g.Import("backbuffer", colorDesc, kFakeBackbufferId,
                                   State::Present, State::Present);

    struct ScenePass { TextureHandle color, depth; };
    g.AddPass<ScenePass>(
        "3D",
        [&](RenderGraph::Builder& b, ScenePass& d) {
            d.color = color = b.SetRenderAttachment(color, 0, LoadOp::Clear);
            d.depth = depth = b.SetDepthAttachment(depth, LoadOp::Clear);
        },
        [](const ScenePass&, CommandContext&) {});

    // execute の中でハンドルから物理リソースを引けることを確かめる
    uint32_t resolved = kInvalidPhysicalId;
    struct CopyPass { TextureHandle src; };
    g.AddPass<CopyPass>(
        "Present",
        [&](RenderGraph::Builder& b, CopyPass& d) {
            d.src = b.SampledRead(color);
            bb    = b.SetRenderAttachment(bb, 0, LoadOp::Clear);
        },
        [&resolved](const CopyPass& d, CommandContext& ctx) {
            resolved = ctx.PhysicalOf(d.src);
        });

    CHECK(h.Compile());
    LoggingCommandContext ctx;
    g.Execute(ctx);

    // 「最初に必要な状態」で作られるので 1 フレーム目に初期バリアが要らない
    CHECK(h.alloc.records.size() == 2);
    if (h.alloc.records.size() == 2) {
        CHECK(h.alloc.records[0].name == "color");
        CHECK(h.alloc.records[0].initialState == State::RenderTarget);
        CHECK(h.alloc.records[1].name == "depth");
        CHECK(h.alloc.records[1].initialState == State::DepthWrite);
    }
    // color は最初から RenderTarget なので遷移は「RT -> SRV」の 1 本だけ
    CheckList("transitions", ctx.Transitions(),
              {
                  "color: RenderTarget -> PixelShaderResource",
                  "backbuffer: Present -> RenderTarget",
                  "backbuffer: RenderTarget -> Present",
              });

    CHECK(resolved != kInvalidPhysicalId);
    CHECK(resolved == g.FindResource("color")->physicalId);
}

// === テスト 1: 実行順とバリア ==============================================
static void TestCurrentGraph() {
    std::printf("[TestCurrentGraph]\n");
    Pooled       h;
    RenderGraph& g = h.graph;

    // --- 1 フレーム目：リソースを「最初に必要な状態」で作るので初期バリアが不要 ---
    BuildCurrentGraph(g);
    CHECK(h.Compile());
    LoggingCommandContext ctx1;
    g.Execute(ctx1);
    h.EndFrame();

    CheckList("frame 1 passes", g.OrderedPassNames(), { "3D", "BlurH", "BlurV", "ImGui" });
    CheckList("frame 1 transitions", ctx1.Transitions(),
              {
                  "pera1: RenderTarget -> PixelShaderResource",
                  "pera2: RenderTarget -> PixelShaderResource",
                  "backbuffer: Present -> RenderTarget",
                  "backbuffer: RenderTarget -> Present",
              });

    // --- 2 フレーム目：定常状態。前フレーム終了時の状態から始まる ---
    g.Clear();
    BuildCurrentGraph(g);
    CHECK(h.Compile());
    LoggingCommandContext ctx2;
    g.Execute(ctx2);
    h.EndFrame();

    // 2 フレーム目は確保が起きない（プールから引いている）
    CHECK(h.alloc.allocateCount == 3);  // pera1 / pera2 / depth。backbuffer は import
    CHECK(h.alloc.AliveCount() == 3);

    // owned は Compile() がプールから physicalId を埋める。
    // 2 フレーム目も同じ物理リソースを引くので id は変わらない。
    CHECK(g.FindResource("pera1")->physicalId == 0);
    CHECK(g.FindResource("pera2")->physicalId == 1);
    CHECK(g.FindResource("depth")->physicalId == 2);
    // imported は Import() で渡した id がそのまま残る
    CHECK(g.FindResource("backbuffer")->physicalId == kFakeBackbufferId);

    // ★ 手書きの Dx12Wrapper が 1 フレームに発行しているバリア 6 個と一致する ★
    CheckList("frame 2 transitions", ctx2.Transitions(),
              {
                  "pera1: PixelShaderResource -> RenderTarget",
                  "pera1: RenderTarget -> PixelShaderResource",
                  "pera2: PixelShaderResource -> RenderTarget",
                  "pera2: RenderTarget -> PixelShaderResource",
                  "backbuffer: Present -> RenderTarget",
                  "backbuffer: RenderTarget -> Present",
              });
    CHECK(g.BarrierCount() == 6);

    // depth は「パス内では効くがパス間の辺にならない」ので遷移ゼロ
    const auto* depth = g.FindResource("depth");
    CHECK(depth != nullptr);
    CHECK(depth->stateAtFrameStart == State::DepthWrite);
    CHECK(depth->stateAtFrameEnd == State::DepthWrite);

    g.Dump();
}

// === テスト 2: 用途フラグが宣言から導出されること ===========================
static void TestUsageFlags() {
    std::printf("[TestUsageFlags]\n");
    Pooled       h;
    RenderGraph& g = h.graph;
    BuildCurrentGraph(g);
    CHECK(h.Compile());

    CHECK(g.FindResource("pera1")->usageFlags == (Usage::RenderTarget | Usage::ShaderResource));
    CHECK(g.FindResource("pera2")->usageFlags == (Usage::RenderTarget | Usage::ShaderResource));
    // DS のみ → DENY_SHADER_RESOURCE を付けられる
    CHECK(g.FindResource("depth")->usageFlags == Usage::DepthStencil);

    // ライフタイム（実行順の添字）
    CHECK(g.FindResource("pera1")->firstUse == 0);
    CHECK(g.FindResource("pera1")->lastUse == 1);
    CHECK(g.FindResource("pera2")->firstUse == 1);
    CHECK(g.FindResource("pera2")->lastUse == 2);
}

// === テスト 3: カリングが連鎖すること =======================================
static void TestCulling() {
    std::printf("[TestCulling]\n");
    Pooled       h;
    RenderGraph& g = h.graph;
    BuildCurrentGraph(g);

    const TextureDesc d{ 1280, 720, Format::RGBA8_UNorm, { 0, 0, 0, 1 }, 1.0f };
    TextureHandle tmp      = g.Create("tmp", d);
    TextureHandle debugOut = g.Create("debugOut", d);

    // tmp を作るパス
    struct Empty {};
    g.AddPass<Empty>(
        "DebugPrepare",
        [&](RenderGraph::Builder& b, Empty&) { tmp = b.SetRenderAttachment(tmp, 0, LoadOp::Clear); },
        [](const Empty&, CommandContext& ctx) { ctx.Draw("debug-prepare"); });

    // tmp を読んで debugOut に書くパス。debugOut は誰も読まない。
    g.AddPass<Empty>(
        "DebugOverlay",
        [&](RenderGraph::Builder& b, Empty&) {
            b.SampledRead(tmp);
            debugOut = b.SetRenderAttachment(debugOut, 0, LoadOp::Clear);
        },
        [](const Empty&, CommandContext& ctx) { ctx.Draw("debug-overlay"); });

    CHECK(h.Compile());

    // ★ カリングされたリソースは物理確保もされない ★
    CHECK(h.alloc.allocateCount == 3);  // pera1 / pera2 / depth のみ
    CHECK(!h.pool.HasEntryFor("tmp"));
    CHECK(!h.pool.HasEntryFor("debugOut"));

    // debugOut を誰も読まない → DebugOverlay が落ちる → tmp が孤立 → DebugPrepare も落ちる
    CHECK(g.IsCulled("DebugOverlay"));
    CHECK(g.IsCulled("DebugPrepare"));
    CheckList("culled order", g.OrderedPassNames(), { "3D", "BlurH", "BlurV", "ImGui" });

    // backbuffer は requiredFinalState を持つのでカリングの根になり、生き残る
    CHECK(!g.IsCulled("BlurV"));
    CHECK(!g.IsCulled("ImGui"));
}

// === テスト 4: シャドウマップを足すと depth が辺になる =======================
static void TestShadowMap() {
    std::printf("[TestShadowMap]\n");
    Pooled       h;
    RenderGraph& g = h.graph;

    const TextureDesc colorDesc{ 1280, 720, Format::RGBA8_UNorm, { 0.5f, 0.5f, 0.5f, 1.0f }, 1.0f };
    const TextureDesc depthDesc{ 1280, 720, Format::D32_Float, { 1, 1, 1, 1 }, 1.0f };
    const TextureDesc shadowDesc{ 1024, 1024, Format::D32_Float, { 1, 1, 1, 1 }, 1.0f };

    TextureHandle pera1  = g.Create("pera1", colorDesc);
    TextureHandle depth  = g.Create("depth", depthDesc);
    TextureHandle shadow = g.Create("shadowDepth", shadowDesc);
    TextureHandle bb     = g.Import("backbuffer", colorDesc, kFakeBackbufferId, State::Present, State::Present);

    struct Empty {};
    g.AddPass<Empty>(
        "ShadowMap",
        [&](RenderGraph::Builder& b, Empty&) {
            shadow = b.SetDepthAttachment(shadow, LoadOp::Clear);
        },
        [](const Empty&, CommandContext& ctx) { ctx.Draw("shadow-casters"); });

    struct ScenePass { TextureHandle shadow; };
    g.AddPass<ScenePass>(
        "3D",
        [&](RenderGraph::Builder& b, ScenePass& d) {
            d.shadow = b.SampledRead(shadow);   // ここで depth がパス間の辺になる
            pera1    = b.SetRenderAttachment(pera1, 0, LoadOp::Clear);
            depth    = b.SetDepthAttachment(depth, LoadOp::Clear);
        },
        [](const ScenePass&, CommandContext& ctx) { ctx.Draw("pmd"); });

    struct BlitPass { TextureHandle src; };
    g.AddPass<BlitPass>(
        "Present",
        [&](RenderGraph::Builder& b, BlitPass& d) {
            d.src = b.SampledRead(pera1);
            bb    = b.SetRenderAttachment(bb, 0, LoadOp::Clear);
        },
        [](const BlitPass&, CommandContext& ctx) { ctx.Draw("blit"); });

    CHECK(h.Compile());

    CheckList("shadow order", g.OrderedPassNames(), { "ShadowMap", "3D", "Present" });

    // ★ 同じ「デプスバッファ」でも宣言が違うので desc が変わる ★
    CHECK(g.FindResource("shadowDepth")->usageFlags == (Usage::DepthStencil | Usage::ShaderResource));
    CHECK(g.FindResource("depth")->usageFlags == Usage::DepthStencil);

    LoggingCommandContext ctx;
    g.Execute(ctx);
    // shadowDepth は DepthWrite で作られ、3D の前に SRV へ遷移する。
    // pera1 も RenderTarget で作られ、Present パスの前に SRV へ遷移する（初回フレーム）。
    CheckList("shadow transitions", ctx.Transitions(),
              {
                  "shadowDepth: DepthWrite -> PixelShaderResource",
                  "pera1: RenderTarget -> PixelShaderResource",
                  "backbuffer: Present -> RenderTarget",
                  "backbuffer: RenderTarget -> Present",
              });
    CHECK(g.BarrierCount() == 4);
    g.Dump();
}


// === テスト 5: プールの再利用（フレームを越えて確保が起きない） ==============
static void TestPoolReuse() {
    std::printf("[TestPoolReuse]\n");
    Pooled h;

    for (int frame = 0; frame < 5; ++frame) {
        h.graph.Clear();
        BuildCurrentGraph(h.graph);
        CHECK(h.Compile());
        LoggingCommandContext ctx;
        h.graph.Execute(ctx);
        h.EndFrame();
    }
    // pera1 / pera2 / depth の 3 枚だけ。backbuffer は import なので確保しない。
    CHECK(h.alloc.allocateCount == 3);
    CHECK(h.alloc.releaseCount == 0);
    CHECK(h.pool.LiveEntryCount() == 3);
}

// === テスト 6: desc が同一でも名前が違えば別リソース ========================
// これが「desc ハッシュだけをキーにできない」理由。pera1 と pera2 は desc が同一で、
// ライフタイムが重なる（[0..1] と [1..2] がパス 1 で重複）ので共有できない。
static void TestIdenticalDescNotShared() {
    std::printf("[TestIdenticalDescNotShared]\n");
    FakeResourceAllocator alloc;
    TexturePool           pool(alloc);
    const TextureDesc     d{ 1280, 720, Format::RGBA8_UNorm, { 0, 0, 0, 1 }, 1.0f };

    pool.BeginFrame();
    const uint32_t e1 = pool.Acquire("pera1", d, Usage::RenderTarget | Usage::ShaderResource);
    const uint32_t e2 = pool.Acquire("pera2", d, Usage::RenderTarget | Usage::ShaderResource);
    CHECK(e1 != kInvalidPoolEntry);
    CHECK(e2 != kInvalidPoolEntry);
    CHECK(pool.PhysicalId(e1) != pool.PhysicalId(e2));
    CHECK(alloc.allocateCount == 2);
}

// === テスト 7: 追い出しと遅延解放（フェンス） ================================
static void TestEvictionAndDeferredRelease() {
    std::printf("[TestEvictionAndDeferredRelease]\n");
    FakeResourceAllocator alloc;
    TexturePool           pool(alloc);
    const TextureDesc     d{ 64, 64, Format::RGBA8_UNorm, { 0, 0, 0, 1 }, 1.0f };
    const size_t          oneTexture = 64 * 64 * 4;
    const uint8_t         usage = Usage::RenderTarget;

    // frame 1-2: a と b の 2 枚を使う
    for (uint64_t f = 1; f <= 2; ++f) {
        pool.BeginFrame();
        pool.Acquire("a", d, usage);
        pool.Acquire("b", d, usage);
        pool.EndFrame(f);
        pool.Reclaim(f - 1);
    }
    CHECK(alloc.allocateCount == 2);
    CHECK(pool.UsedBytes() == oneTexture * 2);

    // frame 3-6: b を要求しない。b.lastUsedFrame = 2 なので 2 + 3 < 6 で frame 6 に追い出される
    for (uint64_t f = 3; f <= 6; ++f) {
        pool.BeginFrame();
        pool.Acquire("a", d, usage);
        pool.EndFrame(f);
        pool.Reclaim(f - 1);
    }
    // ★保留キューに入っただけ。GPU がまだ読んでいる可能性があるので破棄していない★
    CHECK(pool.PendingReleaseCount() == 1);
    CHECK(alloc.releaseCount == 0);
    CHECK(pool.LiveEntryCount() == 1);
    // ★保留中も予算を消費し続ける★
    CHECK(pool.UsedBytes() == oneTexture * 2);

    // frame 7: フェンスが 6 まで進んだので、ここで初めて実際に破棄される
    pool.BeginFrame();
    pool.Acquire("a", d, usage);
    pool.EndFrame(7);
    pool.Reclaim(6);
    CHECK(pool.PendingReleaseCount() == 0);
    CHECK(alloc.releaseCount == 1);
    CHECK(pool.UsedBytes() == oneTexture);
    CHECK(alloc.AliveCount() == 1);
}

// === テスト 8: リサイズ（同名・別 desc） ====================================
static void TestResizeMakesNewResource() {
    std::printf("[TestResizeMakesNewResource]\n");
    FakeResourceAllocator alloc;
    TexturePool           pool(alloc);
    const TextureDesc     d720{ 1280, 720, Format::RGBA8_UNorm, { 0, 0, 0, 1 }, 1.0f };
    const TextureDesc     d1080{ 1920, 1080, Format::RGBA8_UNorm, { 0, 0, 0, 1 }, 1.0f };

    pool.BeginFrame();
    pool.Acquire("pera1", d720, Usage::RenderTarget);
    pool.EndFrame(1);
    pool.Reclaim(0);

    // 名前は同じだが desc が違う → 古いものを返してはいけない
    pool.BeginFrame();
    const uint32_t e = pool.Acquire("pera1", d1080, Usage::RenderTarget);
    CHECK(e != kInvalidPoolEntry);
    CHECK(alloc.allocateCount == 2);
    CHECK(alloc.records[pool.PhysicalId(e)].sizeBytes == 1920u * 1080u * 4u);
    pool.EndFrame(2);
    pool.Reclaim(1);

    // 古い 720 の方はやがて追い出される
    for (uint64_t f = 3; f <= 8; ++f) {
        pool.BeginFrame();
        pool.Acquire("pera1", d1080, Usage::RenderTarget);
        pool.EndFrame(f);
        pool.Reclaim(f - 1);
    }
    CHECK(alloc.releaseCount == 1);
    CHECK(pool.LiveEntryCount() == 1);
}

// === テスト 9: メモリ予算で事前に弾く（ロードマップ B） =====================
static void TestBudget() {
    std::printf("[TestBudget]\n");
    // --- プール単体 ---
    {
        FakeResourceAllocator alloc;
        TexturePool           pool(alloc);
        const TextureDesc     d{ 64, 64, Format::RGBA8_UNorm, { 0, 0, 0, 1 }, 1.0f };
        pool.SetBudgetBytes(64 * 64 * 4 * 2);  // 2 枚分だけ

        pool.BeginFrame();
        CHECK(pool.Acquire("a", d, Usage::RenderTarget) != kInvalidPoolEntry);
        CHECK(pool.Acquire("b", d, Usage::RenderTarget) != kInvalidPoolEntry);
        CHECK(pool.Acquire("c", d, Usage::RenderTarget) == kInvalidPoolEntry);  // 弾かれる
        CHECK(alloc.allocateCount == 2);
    }

    // --- グラフ経由：Compile が false を返し、落ちたリソース名が取れる ---
    {
        Pooled h;
        h.pool.SetBudgetBytes(1280u * 720u * 4u * 2u);  // pera1 / pera2 までで一杯
        BuildCurrentGraph(h.graph);
        CHECK(!h.Compile());
        CheckList("allocation failures", h.graph.AllocationFailures(), { "depth" });
        std::printf("  used=%zu / budget=%zu\n", h.pool.UsedBytes(), h.pool.BudgetBytes());
    }
}

int main() {
    TestCurrentGraph();
    TestAttachments();
    TestInitialStateAndResolve();
    TestUsageFlags();
    TestCulling();
    TestShadowMap();
    TestPoolReuse();
    TestIdenticalDescNotShared();
    TestEvictionAndDeferredRelease();
    TestResizeMakesNewResource();
    TestBudget();

    if (g_failures == 0) {
        std::printf("\nall tests passed\n");
        return 0;
    }
    std::printf("\n%d check(s) failed\n", g_failures);
    return 1;
}
