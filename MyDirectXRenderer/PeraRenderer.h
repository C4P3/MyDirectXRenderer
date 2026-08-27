#pragma once

#include <d3d12.h>
#include <wrl/client.h>    // © ComPtr ‚ğƒƒ“ƒo‚É‚Â‚È‚ç

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
	void DrawHorizontal(); // 1–‡–Ú‚ğ“Ç‚Ş
	void DrawVertical();// 2–‡–Ú‚ğ“Ç‚Ş
	bool Init();
	void Draw(UINT srvIndex, ID3D12PipelineState* pso);
};