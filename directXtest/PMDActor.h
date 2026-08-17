#pragma once

#include <d3d12.h>
#include <vector>
#include <string>          // ← AdditionalMaterial::texPath 用
#include <map>
#include <DirectXMath.h>   // ← XMFLOAT3 用
#include <wrl/client.h>    // ← ComPtr をメンバに持つなら
#include "PMDRenderer.h"

class Dx12Wrapper;

// 頂点データ構造体
struct Vertex
{
	DirectX::XMFLOAT3 pos;
	DirectX::XMFLOAT2 uv;
};

struct PMDHeader
{
	float version;		// 例 : 00 00 80 3F == 1.00
	char model_name[20];// モデル名
	char comment[256];	// モデルコメント
};

// シェーダー側に渡すための基本的な環境データ
struct SceneMatrix
{
	DirectX::XMMATRIX world;
	DirectX::XMMATRIX view;
	DirectX::XMMATRIX proj;
	DirectX::XMMATRIX eye;	// 視点座標
};

struct PMDVertex
{
	DirectX::XMFLOAT3 pos;				// 頂点座標		: 12バイト
	DirectX::XMFLOAT3 normal;			// 法線ベクトル	: 12バイト
	DirectX::XMFLOAT2 uv;				// uv座標		: 8バイト
	unsigned short boneNo[2];	// ボーン番号	: 4バイト
	unsigned char boneWeight;	// ボーン影響度 : 1バイト
	unsigned char edgeFlg;		// 輪郭線フラグ	: 1バイト
	unsigned char padding[2];	// 明示的に2バイト埋める (合計40バイト)
};


class PMDActor {
private:
    Dx12Wrapper& _dx12;
	D3D12_VERTEX_BUFFER_VIEW vbView = {};
	D3D12_INDEX_BUFFER_VIEW ibView = {};
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _basicDescHeap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _vertBuff = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _idxBuff = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _constBuff = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _materialBuff = nullptr;
	std::map<std::string, Microsoft::WRL::ComPtr<ID3D12Resource>> _resourceTable;
	Microsoft::WRL::ComPtr<ID3D12Resource> _whiteTex = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _blackTex = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _grayGradationTex = nullptr;
	unsigned int indicesNum = 0;	// このマテリアルが割り当てられるインデックス数
	std::vector<Material> materials = {};
	SceneMatrix* _mapMatrix = nullptr;
	float angle = 0.0f;
	DirectX::XMMATRIX _worldMatrix = DirectX::XMMatrixRotationY(DirectX::XM_PIDIV4);
public:
	PMDActor(Dx12Wrapper& dx12) : _dx12(dx12) {}
	DirectX::XMMATRIX WorldMatrix() const { return _worldMatrix; }
	SceneMatrix* MapMatrix() const { return _mapMatrix; }
    bool Load(const char* filepath);
    void Update(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj);
    void Draw();
	Microsoft::WRL::ComPtr<ID3D12Resource> LoadTextureFromFile(
		const std::string& filePath
	);
};