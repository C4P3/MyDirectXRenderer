#include "RenderPasses.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"


void Pass1_Main3D::Setup(RenderGraphBuilder& builder) {
    builder.RequireResourceState("PeraResource1", D3D12_RESOURCE_STATE_RENDER_TARGET);
    builder.RequireResourceState("DepthBuffer", D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void Pass1_Main3D::Execute(ID3D12GraphicsCommandList* cmdList) {
    // 1. レンダーターゲットと深度バッファのセット
    auto rtvH = _dx12->PeraRTVHeap()->GetCPUDescriptorHandleForHeapStart();
    auto dsvH = _dx12->GetDSV();
    cmdList->OMSetRenderTargets(1, &rtvH, false, &dsvH);

    // 2. 画面のクリア
    float clearColor[] = { 0.5f, 0.5f, 0.5f, 1.0f };
    cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);
    cmdList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // 3. ビューポートとシザー矩形の設定
    cmdList->RSSetViewports(1, _dx12->GetViewport());
    cmdList->RSSetScissorRects(1, _dx12->GetScissorRect());

    // 4. 実際の描画
    _pmdRenderer->Draw(*_scene);
    _gregoryRenderer->Draw(*_scene);
}

void Pass2_HorizontalBlur::Setup(RenderGraphBuilder& builder) {
    // Pass1で描いた PeraResource1 をテクスチャとして読み込む
    builder.RequireResourceState("PeraResource1", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // 描画先は2枚目
    builder.RequireResourceState("PeraResource2", D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void Pass2_HorizontalBlur::Execute(ID3D12GraphicsCommandList* cmdList) {
    auto rtvH = _dx12->PeraRTVHeap()->GetCPUDescriptorHandleForHeapStart();
    rtvH.ptr += _dx12->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); // 2枚目
    cmdList->OMSetRenderTargets(1, &rtvH, false, nullptr);

    float clearColor[] = { 0.5f, 0.5f, 0.5f, 1.0f };
    cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

    cmdList->RSSetViewports(1, _dx12->GetViewport());
    cmdList->RSSetScissorRects(1, _dx12->GetScissorRect());

    _peraRenderer->DrawHorizontal();
}

void Pass3_VerticalBlurAndUI::Setup(RenderGraphBuilder& builder) {
    builder.RequireResourceState("PeraResource2", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    builder.RequireResourceState("BackBuffer", D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void Pass3_VerticalBlurAndUI::Execute(ID3D12GraphicsCommandList* cmdList) {
    auto rtvH = _dx12->GetCurrentBackBufferRTV();
    cmdList->OMSetRenderTargets(1, &rtvH, true, nullptr);

    float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);

    cmdList->RSSetViewports(1, _dx12->GetViewport());
    cmdList->RSSetScissorRects(1, _dx12->GetScissorRect());

    // 4. 実際の描画
    _peraRenderer->DrawVertical();

    ID3D12DescriptorHeap* imguiHeaps[] = { _dx12->GetHeapForImgui().Get() };
    cmdList->SetDescriptorHeaps(1, imguiHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}