// GroundRenderer — 原点に置いた水平な板。影を受けるためだけのもの
//
// Actor を持たない。形が 4 頂点で固定なので頂点バッファも自分で抱えている
// （PeraRenderer と同じ作り）。影は落とさないので影パスには出てこない。
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

class Dx12Wrapper;
class Scene;

class GroundRenderer
{
private:
	Dx12Wrapper& _dx12;
	Microsoft::WRL::ComPtr<ID3D12Resource> _vertBuff;
	D3D12_VERTEX_BUFFER_VIEW _vbView = {};
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState;
public:
	GroundRenderer(Dx12Wrapper& dx12) : _dx12(dx12) {}
	bool Init();

	// shadowMapSrv は RenderGraph が解決したシャドウマップ
	void Draw(const Scene& scene, D3D12_GPU_DESCRIPTOR_HANDLE shadowMapSrv);
};
