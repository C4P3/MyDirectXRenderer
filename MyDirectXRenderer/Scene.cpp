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

    XMFLOAT4 planeVec(0, 1, 0, 0);
    _mappedScene->shadow = XMMatrixShadow(
        XMLoadFloat4(&planeVec),
        -XMLoadFloat3(&_parallelLightVec)
    );
}

void Scene::DrawDebugGui() {
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("eye", &_eye.x, 0.1f);
        ImGui::DragFloat3("target", &_target.x, 0.1f);
        ImGui::SliderAngle("fovY", &_fovY, 10.0f, 120.0f);
        ImGui::DragFloatRange2("near/far", &_near, &_far, 0.1f, 0.01f, 1000.0f);
    }
}