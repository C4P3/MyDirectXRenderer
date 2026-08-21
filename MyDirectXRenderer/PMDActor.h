#pragma once

#include <d3d12.h>
#include <vector>
#include <array>
#include <string>          // ← AdditionalMaterial::texPath 用
#include <map>
#include <unordered_map>
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

// シェーダーに渡す座標データ
struct Transform
{
	DirectX::XMMATRIX world;
	std::array<DirectX::XMMATRIX, 256> bones;
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

struct BoneNode
{
	int boneIdx;		// ボーンインデックス
	DirectX::XMFLOAT3 startPos;	// ボーン基準点（回転の中心）
	DirectX::XMFLOAT3 endPos;	// ボーン先端点（実際のスキニングには利用しない）
	std::vector<BoneNode*> children;	// 子ノード
};

struct KeyFrame
{
	unsigned int frameNo;	// アニメーション開始からのフレーム数
	DirectX::XMVECTOR quaternion;	// クォータニオン

	// const を追加して一時オブジェクトを受け取れるようにする
	// （DirectXMathの最適化に合わせるなら const DirectX::XMVECTOR& の代わりに DirectX::FXMVECTOR も可）
	KeyFrame(unsigned int fno, const DirectX::XMVECTOR& q)
		: frameNo(fno), quaternion(q)
	{}
};

class PMDActor {
private:
    Dx12Wrapper& _dx12;
	D3D12_VERTEX_BUFFER_VIEW vbView = {};
	D3D12_INDEX_BUFFER_VIEW ibView = {};
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _basicDescHeap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _vertBuff = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _idxBuff = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _transformBuff  = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _materialBuff = nullptr;
	std::map<std::string, Microsoft::WRL::ComPtr<ID3D12Resource>> _resourceTable;
	Microsoft::WRL::ComPtr<ID3D12Resource> _whiteTex = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _blackTex = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> _grayGradationTex = nullptr;
	unsigned int indicesNum = 0;	// このマテリアルが割り当てられるインデックス数
	std::vector<Material> materials = {};
	Transform* _mappedTransform = nullptr;
	float angle = 0.0f;
	DirectX::XMMATRIX _worldMatrix = DirectX::XMMatrixRotationY(DirectX::XM_PIDIV4);
	std::vector<DirectX::XMMATRIX> _boneMatrices = {};
	std::map<std::string, BoneNode> _boneNodeTable = {};
	std::unordered_map<std::string, std::vector<KeyFrame>> _motiondata;
	DWORD _startTime;	// アニメーション開始のミリ秒

	void RecursiveMatrixMultiply(const BoneNode* node, const DirectX::XMMATRIX& mat);
	bool VMDMotionLoad(const char* filepath);
public:
	PMDActor(Dx12Wrapper& dx12) : _dx12(dx12) {}
	DirectX::XMMATRIX WorldMatrix() const { return _worldMatrix; }

    bool Load(const char* filepath);
    void Update();
    void Draw();
	Microsoft::WRL::ComPtr<ID3D12Resource> LoadTextureFromFile(
		const std::string& filePath
	);

	void PlayAnimation();
};