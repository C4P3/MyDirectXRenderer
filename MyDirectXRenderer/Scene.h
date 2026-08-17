#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>
// b0 に送るシーン共通データ。world は入れない
struct SceneData
{
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX proj;
    DirectX::XMFLOAT3 eye;
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
    float _aspect = 1.0f;

public:
    Scene(Dx12Wrapper& dx12) : _dx12(dx12) {}
    bool Init(int width, int height);
    void Update();

    // ルートCBV に渡す GPU 仮想アドレス
    D3D12_GPU_VIRTUAL_ADDRESS SceneCBAddress() const {
        return _sceneBuff->GetGPUVirtualAddress();
    }
};