#pragma once

#include <d3d12.h>
#include <wrl/client.h>    // ← ComPtr をメンバに持つなら

class Dx12Wrapper;

class PeraRenderer
{
private:
	Dx12Wrapper& _dx12;
	
	Microsoft::WRL::ComPtr<ID3D12Resource> _peraVB = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _bokehParamBuffer = nullptr;
	D3D12_VERTEX_BUFFER_VIEW _peraVBV = {};
	

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _psoHorizontal;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _psoVertical;
public:
	PeraRenderer(Dx12Wrapper& dx12) : _dx12(dx12) {}
	// 読むテクスチャは RenderGraph が解決して渡す（どの物理リソースかは
	// パスの SampledRead 宣言で決まるので、ここで添字を知る必要はない）
	void DrawHorizontal(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srv);
	void DrawVertical(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srv);
	bool Init();
	void Draw(ID3D12DescriptorHeap* srvHeap, D3D12_GPU_DESCRIPTOR_HANDLE srv,
		ID3D12PipelineState* pso);
};