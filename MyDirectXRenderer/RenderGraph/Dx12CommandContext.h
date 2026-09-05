// Dx12CommandContext — RHI の継ぎ目その 1（CommandContext）の DX12 実装
//
// RenderGraph が導出したバリアを ID3D12GraphicsCommandList に翻訳する。
// パスの execute ラムダは List() でコマンドリストを取り出して従来通り描画する。
//
// このファイルは UTF-8 (BOM 付き)。
#pragma once

#include <d3d12.h>

#include <string>

#include "Dx12ResourceAllocator.h"
#include "Frontend/CommandContext.h"

D3D12_RESOURCE_STATES ToD3D12(rg::State s);

class Dx12CommandContext : public rg::CommandContext {
public:
    Dx12CommandContext(ID3D12GraphicsCommandList* cmdList, const Dx12ResourceAllocator& allocator)
        : _cmdList(cmdList), _allocator(allocator) {}

    ID3D12GraphicsCommandList* List() const { return _cmdList; }

    // --- rg::CommandContext ---
    void Transition(const std::string& name, uint32_t physicalId,
                    rg::State from, rg::State to) override;
    void BeginPass(const std::string& name) override;
    void EndPass() override;
    void Draw(const std::string& what) override;

private:
    ID3D12GraphicsCommandList*   _cmdList = nullptr;
    const Dx12ResourceAllocator& _allocator;
};
