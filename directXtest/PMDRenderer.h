#pragma once

#include <d3d12.h>
#include <vector>
#include <string>          // ← AdditionalMaterial::texPath 用
#include <DirectXMath.h>   // ← XMFLOAT3 用
#include <wrl/client.h>    // ← ComPtr をメンバに持つなら

class Dx12Wrapper;
class PMDActor;

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
	Dx12Wrapper& _dx12;
	std::vector<PMDActor*> _actors;   // 非所有。所有者は Application
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState;
public:
	PMDRenderer(Dx12Wrapper& dx12) : _dx12(dx12) {}
	void AddActor(PMDActor* actor) { _actors.push_back(actor); }
	bool Init();

	void Draw();
};