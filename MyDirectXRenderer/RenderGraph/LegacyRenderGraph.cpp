#include "LegacyRenderGraph.h"
#include <assert.h>
#include "../d3dx12.h"


void RenderGraph::RegisterResource(const std::string& name,
    ID3D12Resource* pResource,
    D3D12_RESOURCE_STATES initialState) {
    _resourceRegistry[name] = { pResource, initialState, initialState };
}

void RenderGraph::UpdateResource(const std::string& name, ID3D12Resource* pResource) {
    auto it = _resourceRegistry.find(name);
    assert(it != _resourceRegistry.end() && "登録されていないリソースです");
    if (it == _resourceRegistry.end()) return;
    it->second.pResource = pResource;
}

void RenderGraph::AddPass(std::unique_ptr<IRenderPass> pass) {
    _passes.push_back(std::move(pass));
}

// パスから「このリソースをこの状態で使いたい」と要求された時の処理
void RenderGraph::RequireResourceState(const std::string& resourceName,
    D3D12_RESOURCE_STATES requiredState) {
    assert(_inSetup && "Setup 中以外から呼ばれています");

    auto it = _resourceRegistry.find(resourceName);
    assert(it != _resourceRegistry.end() && "登録されていないリソースが要求されました");
    if (it == _resourceRegistry.end()) return;

    auto& resInfo = it->second;
    auto& barriers = _passBarriers[_currentSetupPassIndex];

    // --- 同じパスが同じリソースを再要求した場合は、既存バリアを畳む ---
    for (size_t i = 0; i < barriers.size(); ++i) {
        if (barriers[i].Transition.pResource != resInfo.pResource) continue;

        // A→B のバリアがすでにある。B→C を足すのではなく A→C に書き換える
        if (barriers[i].Transition.StateBefore == requiredState) {
            // A→A になったのでバリア自体が不要
            barriers.erase(barriers.begin() + i);
        }
        else {
            barriers[i].Transition.StateAfter = requiredState;
        }
        resInfo.currentState = requiredState;
        return;
    }

    // 状態が同じならバリア不要
    if (resInfo.currentState == requiredState) return;

    barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
        resInfo.pResource, resInfo.currentState, requiredState));

    resInfo.currentState = requiredState;
}

// すべてのパスの Setup を呼び出し、バリアを事前計算する
void RenderGraph::Compile() {
    // --- 2の対応: フレーム開始状態へリセットしてから計算し直す ---
    for (auto& [name, info] : _resourceRegistry) {
        info.currentState = info.initialState;
    }

    _passBarriers.assign(_passes.size(), {});
    _epilogueBarriers.clear();

    _inSetup = true;
    for (size_t i = 0; i < _passes.size(); ++i) {
        _currentSetupPassIndex = i;
        _passes[i]->Setup(*this);
    }
    _inSetup = false;

    // --- 3の対応: フレーム終端で全リソースを初期状態に戻す ---
    // これでバックバッファの RENDER_TARGET → PRESENT も自動になる
    for (auto& [name, info] : _resourceRegistry) {
        if (info.currentState == info.initialState) continue;

        _epilogueBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            info.pResource, info.currentState, info.initialState));

        info.currentState = info.initialState;
    }
}


// 実際の描画コマンドをコマンドリストに積む
void RenderGraph::Execute(ID3D12GraphicsCommandList* cmdList) {
    for (size_t i = 0; i < _passes.size(); ++i) {
        auto& barriers = _passBarriers[i];
        if (!barriers.empty()) {
            cmdList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }
        _passes[i]->Execute(cmdList);
    }

    if (!_epilogueBarriers.empty()) {
        cmdList->ResourceBarrier(static_cast<UINT>(_epilogueBarriers.size()),
            _epilogueBarriers.data());
    }
}