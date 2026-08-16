#pragma once

#include <d3d12.h>
#include <vector>
#include <string>          // ← AdditionalMaterial::texPath 用
#include <DirectXMath.h>   // ← XMFLOAT3 用
#include <wrl/client.h>    // ← ComPtr をメンバに持つなら

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

#pragma pack(push, 1) // 1バイト境界に設定（パディングを無効化）
struct PMDVertex_Raw
{
	DirectX::XMFLOAT3 pos;			// 12バイト
	DirectX::XMFLOAT3 normal;		// 12バイト
	DirectX::XMFLOAT2 uv;			// 8バイト
	unsigned short boneNo[2];	// 4バイト
	unsigned char boneWeight;	// 1バイト
	unsigned char edgeFlg;		// 1バイト
}; // これで確実に sizeof(PMDVertex_Raw) == 38 になる

// PMD マテリアル構造体
struct PMDMaterial_Raw
{
	DirectX::XMFLOAT3 diffuse;	// ディフューズ色
	float alpha;	// ディフューズα
	float specularity;	// スペキュラの強さ（乗算値）
	DirectX::XMFLOAT3 specular;	// スペキュラ色
	DirectX::XMFLOAT3 ambient;	// アンビエント色
	unsigned char toonIdx;	// トゥーン番号
	unsigned char edgeFlg;	// マテリアルごとの輪郭線フラグ
	// pragma pack(1) によりここに2バイトパディングが発生しない
	unsigned int indicesNum;	// このマテリアルが割り当てられるインデックス数
	char texFilePath[20];	// テクスチャファイルパス
}; // 70バイト
#pragma pack(pop) // 元のアライメント設定に戻す

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
    Dx12Wrapper& _dx12;
public:
    PMDActor(Dx12Wrapper& dx12) : _dx12(dx12) {}
    bool Load(const char* filepath);
    void Update();
    void Draw();
};