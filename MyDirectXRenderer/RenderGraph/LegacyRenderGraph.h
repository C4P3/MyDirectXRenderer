#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <d3d12.h>

// ---------------------------------------------------------
// パスがリソースの要求を宣言するためのビルダー
// ---------------------------------------------------------
class RenderGraphBuilder {
public:
    virtual ~RenderGraphBuilder() = default;

    // パスがリソースを特定の状態で使いたいことを宣言する
    virtual void RequireResourceState(
        const std::string& resourceName, 
        D3D12_RESOURCE_STATES requiredState
    ) = 0;
};

// ---------------------------------------------------------
// レンダーパスの基底インターフェース
// ---------------------------------------------------------
class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    virtual std::string GetName() const = 0;
    virtual void Setup(RenderGraphBuilder& builder) = 0;
    virtual void Execute(ID3D12GraphicsCommandList* cmdList) = 0;
};

// ---------------------------------------------------------
// レンダーグラフ本体
// ---------------------------------------------------------
class RenderGraph : public RenderGraphBuilder {
private:
    struct ResourceInfo {
        ID3D12Resource* pResource = nullptr;
        // フレーム開始時（＝フレーム終了時に戻すべき）状態
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
        // Compile 中に追跡している仮想的な現在状態
        D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
    };

    std::map<std::string, ResourceInfo> _resourceRegistry;
    std::vector<std::unique_ptr<IRenderPass>> _passes;

    // Compileフェーズで決定された、各パスの実行直前に張るべきバリアのリスト
    std::vector<std::vector<D3D12_RESOURCE_BARRIER>> _passBarriers;
    // 全パス終了後、リソースを初期状態に戻すためのバリア
    std::vector<D3D12_RESOURCE_BARRIER> _epilogueBarriers;

    // Setup 中に、今どのパスの Setup を呼んでいるか
    size_t _currentSetupPassIndex = 0;
    bool _inSetup = false;

public:
    // 初期化時に一度だけ呼ぶ。initialState は毎フレーム開始時の状態
    void RegisterResource(const std::string& name,
        ID3D12Resource* pResource,
        D3D12_RESOURCE_STATES initialState);

    // 毎フレーム実体が変わるリソース（バックバッファ）用
    void UpdateResource(const std::string& name, ID3D12Resource* pResource);

    // パスを追加する
    void AddPass(std::unique_ptr<IRenderPass> pass);

    // RenderGraphBuilder の実装（Setup内で呼ばれる）
    void RequireResourceState(const std::string& resourceName, D3D12_RESOURCE_STATES requiredState) override;

    // グラフの構築（バリアの計算）
    void Compile();

    // グラフの実行（コマンドリストへの積み込み）
    void Execute(ID3D12GraphicsCommandList* cmdList);
};