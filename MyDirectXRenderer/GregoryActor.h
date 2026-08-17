#pragma once

#include <d3d12.h>
#include <vector>
#include <string>          // Å© AdditionalMaterial::texPath óp
#include <map>
#include <DirectXMath.h>   // Å© XMFLOAT3 óp
#include <wrl/client.h>    // Å© ComPtr ÇÉÅÉìÉoÇ…éùÇ¬Ç»ÇÁ
#include "core/lattice.h"

class Dx12Wrapper;
class GregoryActor;
struct Transform;

struct GregoryVertex {
	DirectX::XMFLOAT3 pos;
	DirectX::XMFLOAT3 normal;
};

class GregoryActor {
private:
	Dx12Wrapper& _dx12;
	D3D12_VERTEX_BUFFER_VIEW _vbView = {};
	D3D12_INDEX_BUFFER_VIEW _ibView = {};
	unsigned int _indexCount = 0;
	greg::QuadMesh _lattice;
	greg::PatchMesh _patchMesh;
	Microsoft::WRL::ComPtr<ID3D12Resource> _vertBuff = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _idxBuff = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _transformBuff = nullptr;
	Transform* _mappedTransform = nullptr;
	float angle = 0.0f;
	DirectX::XMMATRIX _worldMatrix = DirectX::XMMatrixRotationY(DirectX::XM_PIDIV4);
public:
	GregoryActor(Dx12Wrapper& dx12) : _dx12(dx12) {}
	DirectX::XMMATRIX WorldMatrix() const { return _worldMatrix; }
	bool BuildMesh(int segments);
	void Update();
	void Draw();
};