#pragma once
#include "RenderGraph/RenderGraph.h"
#include "Dx12Wrapper.h"
#include "PMDRenderer.h"
#include "GregoryRenderer.h"
#include "PeraRenderer.h"
#include "Scene.h"

// --- 1枚目に3Dモデルを描画するパス ---
class Pass1_Main3D : public IRenderPass {
private:
    Dx12Wrapper* _dx12;
    PMDRenderer* _pmdRenderer;
    GregoryRenderer* _gregoryRenderer;
    Scene* _scene;

public:
    Pass1_Main3D(Dx12Wrapper* dx12, PMDRenderer* pmd, GregoryRenderer* gregory, Scene* scene)
        : _dx12(dx12), _pmdRenderer(pmd), _gregoryRenderer(gregory), _scene(scene) {
    }

    std::string GetName() const override { return "Pass1_Main3D"; }

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(ID3D12GraphicsCommandList* cmdList) override;
};

// --- 横ぼかしパス ---
class Pass2_HorizontalBlur : public IRenderPass {
private:
    Dx12Wrapper* _dx12;
    PeraRenderer* _peraRenderer;

public:
    Pass2_HorizontalBlur(Dx12Wrapper* dx12, PeraRenderer* pera)
        : _dx12(dx12), _peraRenderer(pera) {
    }

    std::string GetName() const override { return "Pass2_HorizontalBlur"; }

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(ID3D12GraphicsCommandList* cmdList) override;
};

// --- 縦ぼかしとImGuiを描いてバックバッファに出力するパス ---
class Pass3_VerticalBlurAndUI : public IRenderPass {
private:
    Dx12Wrapper* _dx12;
    PeraRenderer* _peraRenderer;

public:
    Pass3_VerticalBlurAndUI(Dx12Wrapper* dx12, PeraRenderer* pera)
        : _dx12(dx12), _peraRenderer(pera) {
    }

    std::string GetName() const override { return "Pass3_VerticalBlurAndUI"; }

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(ID3D12GraphicsCommandList* cmdList) override;
};