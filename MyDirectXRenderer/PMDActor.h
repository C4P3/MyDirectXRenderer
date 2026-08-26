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
	uint32_t boneIdx;		// ボーンインデックス
	uint32_t boneType;		// ボーン種別
	uint32_t ikParentBone;	// IK 親ボーン
	DirectX::XMFLOAT3 startPos;	// ボーン基準点（回転の中心）
	std::vector<BoneNode*> children;	// 子ノード
};

struct PMDIK
{
	uint16_t boneIdx;	// IK 対象のボーンを示す
	uint16_t targetIdx;	// ターゲットに近づけるためのボーンのインデックス
	uint16_t iterations;// 試行回数
	float limit;		// 1回あたりの回転制限
	std::vector<uint16_t> nodeIdxes; // 間のノード番号
};

struct KeyFrame
{
	unsigned int frameNo;			// アニメーション開始からのフレーム数
	DirectX::XMVECTOR quaternion;	// クォータニオン
	DirectX::XMFLOAT3 offset;		// IKの初期座標からのオフセット情報
	DirectX::XMFLOAT2 p1, p2;			// 回転用 ベジェ曲線の中間コントロールポイント
	DirectX::XMFLOAT2 tp1[3], tp2[3];	// 移動 X / Y / Z 用

	KeyFrame(
		unsigned int frameNo = 0,
		DirectX::FXMVECTOR quaternion = DirectX::XMVectorZero(),
		const DirectX::XMFLOAT3& offset = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
		const DirectX::XMFLOAT2& p1 = DirectX::XMFLOAT2(0.0f, 0.0f),
		const DirectX::XMFLOAT2& p2 = DirectX::XMFLOAT2(0.0f, 0.0f),
		const DirectX::XMFLOAT2* inTp1 = nullptr,
		const DirectX::XMFLOAT2* inTp2 = nullptr
	)
		: frameNo(frameNo)
		, quaternion(quaternion)
		, offset(offset)
		, p1(p1)
		, p2(p2)
	{
		for (int i = 0; i < 3; ++i)
		{
			tp1[i] = inTp1 ? inTp1[i] : DirectX::XMFLOAT2(0.0f, 0.0f);
			tp2[i] = inTp2 ? inTp2[i] : DirectX::XMFLOAT2(0.0f, 0.0f);
		}
	}
};

// IK オンオフデータ
struct VMDIKEnable
{
	// キーフレームがあるフレーム番号
	uint32_t frameNo;

	// 名前とオンオフフラグのマップ
	std::unordered_map<std::string, bool> ikEnableTable;
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
	unsigned int _duration = 0;
	std::vector<std::string> _boneNameArray;
	std::vector<BoneNode*> _boneNodeAddressArray;
	std::vector<PMDIK> _ikData;
	std::vector<uint32_t> _kneeIdxes;
	std::vector<VMDIKEnable> _ikEnableData;

	Microsoft::WRL::ComPtr<ID3D12Resource> LoadTextureFromFile(const std::string& filePath);
	void RecursiveMatrixMultiply(const BoneNode* node, const DirectX::XMMATRIX& mat);
	float GetYFromXOnBezier(float x, const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b, uint8_t n);

	void IKSolve(unsigned int frameNo);
	// CCD-IK によりボーン方向を解決する
	// @param ik 対象 IK オブジェクト
	void SolveCCDIK(const PMDIK& ik);
	// 余弦定理 IK によりボーン方向を解決する
	// @param ik 対象 IK オブジェクト
	void SolveCosineIK(const PMDIK& ik);
	// LookAt 行列によりボーン方向を解決
	// @param ik 対象 IK オブジェクト
	void SolveLookAt(const PMDIK& ik);
public:
	PMDActor(Dx12Wrapper& dx12) : _dx12(dx12) {}
	DirectX::XMMATRIX WorldMatrix() const { return _worldMatrix; }

    bool Load(const char* filepath);
	bool VMDMotionLoad(const char* filepath);
    void Update();
    void Draw();

	void PlayAnimation();
};