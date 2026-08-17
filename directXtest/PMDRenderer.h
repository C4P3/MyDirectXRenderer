#pragma once

#include <d3d12.h>
#include <vector>
#include <string>          // ← AdditionalMaterial::texPath 用
#include <DirectXMath.h>   // ← XMFLOAT3 用
#include <wrl/client.h>    // ← ComPtr をメンバに持つなら

class Dx12Wrapper;

// シェーダー側に投げられるマテリアルデータ
struct MaterialForHlsl
{
	DirectX::XMFLOAT3 diffuse;	// ディフューズ色
	float alpha;	// ディフューズα
	DirectX::XMFLOAT3 specular;	// スペキュラ色
	float specularity;	// スペキュラの強さ（乗算値）
	DirectX::XMFLOAT3 ambient;	// アンビエント色
};

// それ以外のマテリアルデータ
struct AdditionalMaterial
{
	std::string texPath;	// テクスチャファイルパス
	unsigned char toonIdx;	// トゥーン番号
	unsigned char edgeFlg;	// マテリアルごとの輪郭線フラグ
};

// 全体をまとめるマテリアルデータ
struct Material
{
	unsigned int indicesNum; // インデックス数
	MaterialForHlsl material;
	AdditionalMaterial additional;
};

class PMDRenderer
{
private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState;
public:
	bool Init(Dx12Wrapper& dx12);

	void Draw(Dx12Wrapper& dx12,
		const D3D12_VERTEX_BUFFER_VIEW& vbView,
		const D3D12_INDEX_BUFFER_VIEW& ibView,
		ID3D12DescriptorHeap* descHeap,
		int indicesNum, const std::vector<Material>& materials
	);
};