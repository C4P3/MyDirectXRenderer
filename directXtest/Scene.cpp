#include <windows.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "Scene.h"
#include "d3dx12.h"
#include "Dx12Wrapper.h"

using namespace DirectX;


bool Scene::Init(int width, int height)
{
    _aspect = static_cast<float>(width) / static_cast<float>(height);

    size_t cbSize = (sizeof(SceneData) + 255) & ~255;   // 256 ƒAƒ‰ƒCƒ“ƒƒ“ƒg
    auto heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto resdesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

    if (FAILED(_dx12.Device()->CreateCommittedResource(
        &heapprop, D3D12_HEAP_FLAG_NONE, &resdesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&_sceneBuff)))) return false;

    CD3DX12_RANGE readRange(0, 0);
    if (FAILED(_sceneBuff->Map(0, &readRange, (void**)&_mappedScene))) return false;

    Update();   // ‰‰ñ‘‚«‚İ
    return true;
}

void Scene::Update()
{
    _mappedScene->view = XMMatrixLookAtLH(
        XMLoadFloat3(&_eye), XMLoadFloat3(&_target), XMLoadFloat3(&_up));
    _mappedScene->proj = XMMatrixPerspectiveFovLH(XM_PIDIV2, _aspect, 1.0f, 100.0f);
    _mappedScene->eye = _eye;   // © ¡‚Ü‚Å‘‚«–Y‚ê‚Ä‚¢‚½‰ÓŠ
}