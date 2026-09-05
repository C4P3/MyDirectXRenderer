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

void Dx12CommandContext::BeginPass(const std::string&, const rg::PassAttachments& att) {
    // --- 書き込み先を並べる ---
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    const UINT colorCount = static_cast<UINT>(att.colors.size());
    assert(colorCount <= D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);

    for (UINT i = 0; i < colorCount; ++i) {
        rtvs[i] = _allocator.RtvOf(att.colors[i].physicalId);
        assert(rtvs[i].ptr != 0 && "RTV が登録されていないリソースに書こうとしている");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
    if (att.hasDepth) {
        dsv = _allocator.DsvOf(att.depth.physicalId);
        assert(dsv.ptr != 0 && "DSV が登録されていないリソースに書こうとしている");
    }

    _cmdList->OMSetRenderTargets(colorCount, colorCount > 0 ? rtvs : nullptr, FALSE,
                                 att.hasDepth ? &dsv : nullptr);

    // --- LoadOp::Clear の宣言だけでクリアが決まる ---
    for (UINT i = 0; i < colorCount; ++i) {
        if (att.colors[i].load == rg::LoadOp::Clear)
            _cmdList->ClearRenderTargetView(rtvs[i], att.colors[i].clearColor, 0, nullptr);
    }
    if (att.hasDepth && att.depth.load == rg::LoadOp::Clear) {
        _cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH,
                                        att.depth.clearDepth, 0, 0, nullptr);
    }

    // --- ビューポートは書き込み先のサイズから決める ---
    // 解像度の違うパス（シャドウマップなど）が来ても、パス側で気にしなくてよくなる。
    const rg::Attachment* size = nullptr;
    if (colorCount > 0)   size = &att.colors[0];
    else if (att.hasDepth) size = &att.depth;

    if (size != nullptr) {
        D3D12_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width    = static_cast<float>(size->width);
        viewport.Height   = static_cast<float>(size->height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        D3D12_RECT scissor = {};
        scissor.left   = 0;
        scissor.top    = 0;
        scissor.right  = static_cast<LONG>(size->width);
        scissor.bottom = static_cast<LONG>(size->height);

        _cmdList->RSSetViewports(1, &viewport);
        _cmdList->RSSetScissorRects(1, &scissor);
    }
}

void Dx12CommandContext::EndPass() {}

void Dx12CommandContext::Draw(const std::string&) {
    // Mac のログ実装用のマーカー。DX12 側では実際の描画を execute ラムダが直接行う。
}
