#pragma once

#include <d3d12.h>
#include <vector>
#include <string>          // ← AdditionalMaterial::texPath 用
#include <DirectXMath.h>   // ← XMFLOAT3 用
#include <wrl/client.h>    // ← ComPtr をメンバに持つなら

class Dx12Wrapper;
class GregoryActor;
class Scene;

class GregoryRenderer
{
private:
	Dx12Wrapper& _dx12;
	std::vector<GregoryActor*> _actors;   // 非所有。所有者は Application
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState;
public:
	GregoryRenderer(Dx12Wrapper& dx12) : _dx12(dx12) {}
	void AddActor(GregoryActor* actor) { _actors.push_back(actor); }
	bool Init();
	void Draw(const Scene& scene);
};