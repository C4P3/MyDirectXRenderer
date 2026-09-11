#include <windows.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "Scene.h"
#include "d3dx12.h"
#include "Dx12Wrapper.h"
#include "Debug.h"
#include "imgui.h"

using namespace DirectX;


bool Scene::Init(int width, int height)
{
    _aspect = static_cast<float>(width) / static_cast<float>(height);

    size_t cbSize = (sizeof(SceneData) + 255) & ~255;   // 256 アラインメント
    auto heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto resdesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

    if (FAILED(_dx12.Device()->CreateCommittedResource(
        &heapprop, D3D12_HEAP_FLAG_NONE, &resdesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&_sceneBuff))))
        return DebugFail("Scene::Init", "シーン用定数バッファの生成");

    CD3DX12_RANGE readRange(0, 0);
    if (FAILED(_sceneBuff->Map(0, &readRange, (void**)&_mappedScene)))
        return DebugFail("Scene::Init", "シーン用定数バッファの Map");

    Update();   // 初回書き込み
    return true;
}

void Scene::Update()
{
    _mappedScene->view = XMMatrixLookAtLH(
        XMLoadFloat3(&_eye), XMLoadFloat3(&_target), XMLoadFloat3(&_up));
    _mappedScene->proj = XMMatrixPerspectiveFovLH(_fovY, _aspect, _near, _far);
    _mappedScene->eye = _eye;

    // ライトの向き。シェーダ側と食い違わないよう、正規化したものを b0 に載せる
    XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&_parallelLightVec));
    XMStoreFloat3(&_mappedScene->lightVec, lightDir);

    // ライトカメラは「_shadowCenter を中心とする半径 _shadowRadius の球」に合わせる。
    // カメラの位置とは無関係にしておかないと、視点を動かすたびに影の範囲が変わってしまう。
    XMVECTOR center = XMLoadFloat3(&_shadowCenter);
    XMVECTOR lightPos = center - lightDir * (_shadowRadius * 2.0f);

    // ライトから見て球は距離 R〜3R に収まるので、near / far はその外側に取る
    XMMATRIX lightView = XMMatrixLookAtLH(lightPos, center, XMLoadFloat3(&_up));
    XMMATRIX lightProj = XMMatrixOrthographicLH(
        _shadowRadius * 2.0f, _shadowRadius * 2.0f,
        _shadowRadius * 0.5f, _shadowRadius * 4.0f);

    _mappedScene->lightCamera = lightView * lightProj;
}

void Scene::DrawDebugGui() {
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("eye", &_eye.x, 0.1f);
        ImGui::DragFloat3("target", &_target.x, 0.1f);
        ImGui::SliderAngle("fovY", &_fovY, 10.0f, 120.0f);
        ImGui::DragFloatRange2("near/far", &_near, &_far, 0.1f, 0.01f, 1000.0f);
    }
    if (ImGui::CollapsingHeader("Light / Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("light dir", &_parallelLightVec.x, 0.05f);
        ImGui::DragFloat3("shadow center", &_shadowCenter.x, 0.5f);
        // 小さくすると影は細かくなるが、範囲から出たものは影を落とさなくなる
        ImGui::DragFloat("shadow radius", &_shadowRadius, 0.5f, 1.0f, 200.0f);
    }
}