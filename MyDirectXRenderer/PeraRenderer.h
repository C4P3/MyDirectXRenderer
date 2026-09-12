#pragma once

#include <d3d12.h>
#include <wrl/client.h>    // ← ComPtr をメンバに持つなら

class Dx12Wrapper;
class Dx12ResourceAllocator;
class Scene;

enum class Effect : size_t
{
	BlurHorizontal,
	BlurVertical,
	Distortion,
	DepthVisualize,
	Through,
	LinearDepthVisualize,
	Bloom,
	Count
};

// ペラポリゴンの書き込み先フォーマット。PSO は RTV のフォーマットと一致していないと
// 作れないので、同じエフェクトでも書き込み先が違えば別の PSO が要る。
enum class TargetFormat : size_t
{
	Color,	// バックバッファやオフスクリーン（SRGB）
	HDR,	// 高輝度バッファと縮小バッファ（float16）
	Count
};

class PeraRenderer
{
private:
	Dx12Wrapper& _dx12;
	
	Microsoft::WRL::ComPtr<ID3D12Resource> _peraVB = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _bokehParamBuffer = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _normalMap;
	D3D12_GPU_DESCRIPTOR_HANDLE _normalMapSrv = {};
	D3D12_VERTEX_BUFFER_VIEW _peraVBV = {};
	

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>
		_psos[static_cast<size_t>(TargetFormat::Count)][static_cast<size_t>(Effect::Count)];
public:
	PeraRenderer(Dx12Wrapper& dx12) : _dx12(dx12) {}
	// 読むテクスチャは RenderGraph が解決して渡す（どの物理リソースかは
	// パスの SampledRead 宣言で決まるので、ここで添字を知る必要はない）
	bool Init(Dx12ResourceAllocator& allocator);
	// srv2 は t2 に張る 2 枚目のテクスチャ。ブルームの合成で縮小バッファを渡すのに使う。
	// 省略したときは t0 と同じものを張る（t2 を読まないエフェクト用のダミー）
	void Draw(const Scene& scene, D3D12_GPU_DESCRIPTOR_HANDLE srv, Effect effect,
		TargetFormat target = TargetFormat::Color,
		D3D12_GPU_DESCRIPTOR_HANDLE srv2 = {});
	// 書き込み先の一部に描く。デバッグ表示と縮小バッファ用
	void DrawTile(const Scene& scene, D3D12_GPU_DESCRIPTOR_HANDLE srv, Effect effect,
		float x, float y, float w, float h,
		TargetFormat target = TargetFormat::Color);
};