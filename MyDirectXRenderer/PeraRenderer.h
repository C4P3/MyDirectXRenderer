#pragma once

#include <d3d12.h>
#include <wrl/client.h>    // © ComPtr ‚ğƒƒ“ƒo‚É‚Â‚È‚ç

class Dx12Wrapper;

class PeraRenderer
{
private:
	Dx12Wrapper& _dx12;
	
	Microsoft::WRL::ComPtr<ID3D12Resource> _peraVB = nullptr;
	D3D12_VERTEX_BUFFER_VIEW _peraVBV = {};
	

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState;
public:
	PeraRenderer(Dx12Wrapper& dx12) : _dx12(dx12) {}
	bool Init();
	void Draw();
};