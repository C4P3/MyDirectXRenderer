#include "Dx12CommandContext.h"

#include <assert.h>

#include "../d3dx12.h"

D3D12_RESOURCE_STATES ToD3D12(rg::State s) {
    switch (s) {
    case rg::State::Present:             return D3D12_RESOURCE_STATE_PRESENT;
    case rg::State::RenderTarget:        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case rg::State::PixelShaderResource: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case rg::State::DepthWrite:          return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case rg::State::Undefined:
    default:
        // Undefined は「指定なし」の番兵。バリアの端点に来ることはない。
        assert(false && "rg::State::Undefined has no D3D12 counterpart");
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

void Dx12CommandContext::Transition(const std::string& name, uint32_t physicalId,
                                    rg::State from, rg::State to) {
    (void)name;  // デバッグ用。実体を指すのは physicalId の方

    ID3D12Resource* res = _allocator.Resolve(physicalId);
    assert(res != nullptr && "physicalId が解決できない（Import に渡す id が間違っている）");
    if (res == nullptr) return;

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(res, ToD3D12(from), ToD3D12(to));
    _cmdList->ResourceBarrier(1, &barrier);
}

void Dx12CommandContext::BeginPass(const std::string&) {
    // 段階 2 でここにアタッチメント情報を渡し、
    // OMSetRenderTargets / Clear*View / RSSetViewports を移してくる。
}

void Dx12CommandContext::EndPass() {}

void Dx12CommandContext::Draw(const std::string&) {
    // Mac のログ実装用のマーカー。DX12 側では実際の描画を execute ラムダが直接行う。
}
