#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>
// b0 に送るシーン共通データ。world は入れない。
// シェーダ側の宣言は Shader/SceneShaderHeader.hlsli 1 箇所にまとめてある。
// HLSL の float3 は 16 バイト境界をまたげないので、eye の後ろに 1 行分の隙間ができる。
// nearZ, farZ はその隙間を C++ 側でも同じように空けるためのもの
struct SceneData
{
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX proj;
    DirectX::XMMATRIX lightCamera;
    DirectX::XMFLOAT3 eye;
    float             nearZ = 1.0f; // 深度の線形化に使う
    DirectX::XMFLOAT3 lightVec;   // 正規化済みの平行光線ベクトル
    float             farZ = 100.0f; // 深度の線形化に使う
};

class Dx12Wrapper;
class Scene
{
private:
    Dx12Wrapper& _dx12;
    Microsoft::WRL::ComPtr<ID3D12Resource> _sceneBuff;
    SceneData* _mappedScene = nullptr;      // 永続マップ。外には出さない

    DirectX::XMFLOAT3 _eye{ 0, 15, -35 };
    DirectX::XMFLOAT3 _target{ 0, 10, 0 };
    DirectX::XMFLOAT3 _up{ 0, 1, 0 };
    DirectX::XMFLOAT3 _parallelLightVec{ 1, -1, 1};

    // シャドウマップに収める範囲。この球がちょうど収まるようにライトを置く。
    // カメラの位置から決めると、カメラを動かすたびに影の解像度と範囲が変わってしまう
    DirectX::XMFLOAT3 _shadowCenter{ 0, 10, 0 };
    float _shadowRadius = 20.0f;
    float _aspect = 1.0f;
    float _fovY = DirectX::XM_PIDIV4;
    float _near = 1.0f;
    float _far = 100.0f;

public:
    Scene(Dx12Wrapper& dx12) : _dx12(dx12) {}
    bool Init(int width, int height);
    void Update();
    void DrawDebugGui();

    // ルートCBV に渡す GPU 仮想アドレス
    D3D12_GPU_VIRTUAL_ADDRESS SceneCBAddress() const {
        return _sceneBuff->GetGPUVirtualAddress();
    }
};