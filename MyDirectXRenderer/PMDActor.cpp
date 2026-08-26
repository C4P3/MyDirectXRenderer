#define MATERIAL_MULTIPLIER 5

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <wrl/client.h> // ComPtr用
#include <string>
#include <DirectXMath.h>
#include <DirectXTex.h>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <map>

#include "d3dx12.h"
#include "Application.h"
#include "Dx12Wrapper.h"
#include "PMDActor.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "DirectXTex.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "winmm.lib")

#ifdef _DEBUG
#include <iostream>
#include <string_view>
#endif

using namespace std;
using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace fs = std::filesystem;

namespace // 無名名前空間
{
	// ボーン種別
	enum class BoneType
	{
		Rotation,	// 回転
		RotAndMove,	// 回転＆移動
		IK,			// IK
		Undefined,	// 未定義
		IKChild,	// IK 影響ボーン
		RotationChild,	// 回転影響ボーン
		IKDestination,	// IK 接続先
		Invisible		// 見えないボーン
	};
}

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

// 読み込み用ボーン構造体
struct PMDBone
{
	char boneName[20];			// ボーン名
	unsigned short parentNo;	// 親ボーン番号
	unsigned short nextNo;		// 先端のボーン番号
	unsigned char type;			// ボーン種別
	unsigned short ikBoneNo;	// IK ボーン番号
	XMFLOAT3 pos;				// ボーンの基準点座標
};

struct VMDMotion_Raw
{
	char boneName[15];	// ボーン名
	unsigned int frameNo;// フレーム番号
	XMFLOAT3 location;	// 位置
	XMFLOAT4 quaternion;	// クォータニオン
	unsigned char bezier[64]; // ベジェ補間パラメータ
};

// 表情データ
struct VMDMorph
{
	char name[15];		// 名前
	uint32_t frameNo;	// フレーム番号
	float weight;		// ウェイト（0.0f ~ 1.0f）
};  // 23 バイト

// カメラ
struct VMDCamera
{
	uint32_t frameNo;	// フレーム番号
	float distance;		// 距離
	XMFLOAT3 pos;		// 座標
	XMFLOAT3 eulerAngle;// オイラー角
	uint8_t Interpolation[24]; // 補間
	uint32_t fov;		// 視野角
	uint8_t persFlg;	// パースフラグ ON / OFF
};  // 61 バイト

// セルフ影データ
struct VMDSelfShadow
{
	uint32_t frameNo;	// フレーム番号
	uint8_t mode;		// 影モード（0:影なし 1:モード１ 2:モード２）
	float distance;		// 距離
};
#pragma pack(pop) // 元のアライメント設定に戻す

// ライト照明データ
struct VMDLight
{
	uint32_t frameNo;	// フレーム番号
	XMFLOAT3 rgb;		// ライト色
	XMFLOAT3 vec;		// 光線ベクトル（平行光線）
};

// パス合成関数
// モデルのパスとテクスチャのパスから合成パスを得る
// @param modelPath アプリケーションから見た pmd モデルのパス
// @param texPath PMD モデルから見たテクスチャのパス
// @return アプリケーションから見たテクスチャのパス
std::string GetTexturePathFromModelAndTexPath(const std::string& modelPath, const char* texPath)
{

	auto folderPath = modelPath.substr(0,
		[&] {
			auto p = modelPath.find_last_of("/\\");
			return p == std::string::npos ? 0 : p + 1;
		}()
			);
	return folderPath + texPath;
}

// std::string（マルチバイト文字列）からstd::wstring（ワイド文字列）を得る
// @param str マルチバイト文字列
// @return 変換されたワイド文字列
std::wstring GetWideStringFromString(const std::string& str)
{
	// 呼び出し1回目（文字列数を得る）
	auto num1 = MultiByteToWideChar(
		CP_ACP,
		MB_PRECOMPOSED | MB_ERR_INVALID_CHARS,
		str.c_str(),
		-1,
		nullptr,
		0
	);

	std::wstring wstr; // string の wchar_t 版
	wstr.resize(num1);

	// 呼び出し2回目（確保済みのwstrに変換文字列をコピー）
	auto num2 = MultiByteToWideChar(
		CP_ACP,
		MB_PRECOMPOSED | MB_ERR_INVALID_CHARS,
		str.c_str(),
		-1,
		&wstr[0],
		num1
	);
	assert(num1 == num2); // 一応チェック
	return wstr;
}

// 拡張子を小文字で取得するヘルパー（大文字小文字の表記揺れ対策）
std::string GetLowerExt(const std::string& pathStr) {
	std::string ext = fs::path(pathStr).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	return ext;
}


ComPtr<ID3D12Resource> PMDActor::LoadTextureFromFile(
	const std::string& filePath
)
{
	// テーブル内にあればロードせずにマップ内のリソースを返す
	auto it = _resourceTable.find(filePath);
	if(it != _resourceTable.end())
	{
		return it->second;
	}

	// 1. 拡張子を取得して小文字化
	std::string ext = GetLowerExt(filePath);

	// 2. ワイド文字列に変換
	std::wstring wFilePath = GetWideStringFromString(filePath);

	TexMetadata metadata = {};
	ScratchImage scratchImg = {};
	HRESULT hr = S_OK;

	// 3. 拡張子に応じて DirectXTex のロード関数を呼び分ける
	if (ext == ".dds") {
		hr = LoadFromDDSFile(wFilePath.c_str(), DDS_FLAGS_NONE, &metadata, scratchImg);
	}
	else if (ext == ".tga") {
		hr = LoadFromTGAFile(wFilePath.c_str(), &metadata, scratchImg);
	}
	else if (ext == ".sph" || ext == ".spa" || ext == ".bmp" || ext == ".png" || ext == ".jpg") {
		hr = LoadFromWICFile(wFilePath.c_str(), WIC_FLAGS_NONE, &metadata, scratchImg);
	}
	else {
		// 未対応のフォーマット
		return nullptr;
	}

	// 読み込み失敗時は nullptr を返す
	if (FAILED(hr)) return nullptr;

	auto img = scratchImg.GetImage(0, 0, 0); // 生データ抽出

	// リソース生成と転送を共通関数に委譲
	return _dx12.CreateTextureFromData(
		metadata.width,
		metadata.height,
		metadata.format,
		img->pixels,
		img->rowPitch,
		img->slicePitch
	);
}

// IK デバッグ用
void IKDebug(std::map<std::string, BoneNode> _boneNodeTable, std::vector<PMDIK> pmdIkData) {
	auto getNameFromIdx = [&](uint16_t idx)->string
		{
			auto it = find_if(_boneNodeTable.begin(), _boneNodeTable.end(),
				[idx](const std::pair<string, BoneNode>& obj)
				{
					return obj.second.boneIdx == idx;
				}
			);

			if (it != _boneNodeTable.end())
			{
				return it->first;
			}
			else
			{
				return "";
			}
		};

	for (auto& ik : pmdIkData)
	{
		std::ostringstream oss;
		oss << "IK ボーン番号 =" << ik.boneIdx
			<< ":" << getNameFromIdx(ik.boneIdx) << '\n';

		for (const auto& node : ik.nodeIdxes)
		{
			oss << "\t ノードボーン =" << node
				<< ":" << getNameFromIdx(node) << '\n';
		}

		OutputDebugStringA(oss.str().c_str());
	}
}

void PMDActor::IKSolve(unsigned int frameNo)
{
	auto it = find_if(_ikEnableData.rbegin(), _ikEnableData.rend(),
		[frameNo](const VMDIKEnable& ikenable)
		{
			return ikenable.frameNo <= frameNo;
		}
	);

	for (auto& ik : _ikData)
	{
		// IK オンオフ情報をフレーム番号で逆から検索
		if (it != _ikEnableData.rend())
		{
			auto ikEnableIt = it->ikEnableTable.find(_boneNameArray[ik.boneIdx]);

			if ((ikEnableIt != it->ikEnableTable.end()) && (!ikEnableIt->second))continue;
		}

		auto childrenNodesCount = ik.nodeIdxes.size();

		switch (childrenNodesCount)
		{
		case 0: // 間のボーン数が 0 (ありえない)
			assert(0);
			continue;
		case 1:	// 間のボーン数が 1 のときは LookAt
			SolveLookAt(ik);
			break;
		case 2:	// 間のボーン数が 2 のときは余弦定理 IK
			SolveCosineIK(ik);
			break;
		default:	// 3以上のときは CCD-IK
			SolveCCDIK(ik);
		}
	}
}

// z 軸を特定の方向に向ける行列を返す関数
// @param lookat 向かせたい方向ベクトル
// @param up 上ベクトル
// @param right 右ベクトル
// z 軸を特定の方向に向ける行列を返す関数
XMMATRIX LookAtMatrix(const XMVECTOR& lookat, const XMFLOAT3& up, const XMFLOAT3& right)
{
	// 向かせたい方向（z軸）
	XMVECTOR vz = lookat;

	// （向かせたい方向を向かせたときの）仮の y 軸ベクトル
	XMVECTOR vy = XMVector3Normalize(XMLoadFloat3(&up));

	// （向かせたい方向を向かせたときの）y 軸
	// XMVECTOR vx = XMVector3Normalize(XMVector3Cross(vz, vx));
	XMVECTOR vx = XMVector3Normalize(XMVector3Cross(vy, vz));
	vy = XMVector3Normalize(XMVector3Cross(vz, vx));

	// LookAt と up が同じ方向を向いていたら right を基準にして作り直す
	if (std::abs(XMVector3Dot(vy, vz).m128_f32[0]) == 1.0f)
	{
		// 仮の x 方向を定義
		vx = XMVector3Normalize(XMLoadFloat3(&right));

		// 向かせたい方向を向かせたときの Y 軸を計算
		vy = XMVector3Normalize(XMVector3Cross(vz, vx));

		// 真の x 軸を計算
		vx = XMVector3Normalize(XMVector3Cross(vy, vz));
	}

	XMMATRIX ret = XMMatrixIdentity();
	ret.r[0] = vx;
	ret.r[1] = vy;
	ret.r[2] = vz;
	return ret;
}

// 特定のベクトルを特定の方向に向けるための行列を返す
// @param origin 特定のベクトル
// @param lookat 向かせたい方向
// @param up 上ベクトル 
// @param right 右ベクトル
// @retval 特定のベクトルを特定の方向に向けるための行列
XMMATRIX LookAtMatrix(const XMVECTOR& dir, const XMFLOAT3& up, const XMFLOAT3& right);

// 4引数版の関数定義（引数を const XMFLOAT3& に変更）
XMMATRIX LookAtMatrix(const XMVECTOR& origin, const XMVECTOR& lookat, const XMFLOAT3& up, const XMFLOAT3& right)
{
	return XMMatrixTranspose(LookAtMatrix(origin, up, right)) * LookAtMatrix(lookat, up, right);
}

void PMDActor::SolveLookAt(const PMDIK& ik)
{
	// この関数に来た時点でノードは１つしかなく、
	// チェーンに入っているノード番号は IK のルートノードのものなので、
	// このルートノードから末端に向かうベクトルを考える
	auto rootNode = _boneNodeAddressArray[ik.nodeIdxes[0]];
	auto targetNode = _boneNodeAddressArray[ik.boneIdx];

	auto rpos1 = XMLoadFloat3(&rootNode->startPos);
	auto tpos1 = XMLoadFloat3(&targetNode->startPos);

	auto rpos2 = XMVector3TransformCoord(rpos1, _boneMatrices[ik.nodeIdxes[0]]);
	auto tpos2 = XMVector3TransformCoord(tpos1, _boneMatrices[ik.boneIdx]);

	auto originVec = XMVectorSubtract(tpos1, rpos1);
	auto targetVec = XMVectorSubtract(tpos2, rpos2);

	originVec = XMVector3Normalize(originVec);
	targetVec = XMVector3Normalize(targetVec);
	_boneMatrices[ik.nodeIdxes[0]] = LookAtMatrix(originVec, targetVec, XMFLOAT3(0, 1, 0), XMFLOAT3(1, 0, 0));
}

bool PMDActor::VMDMotionLoad(const char* filepath) {
	std::vector<VMDMotion_Raw> vmdMotionData_Raw;
	std::vector<VMDMorph> morphs;
	std::vector<VMDCamera> cameraData;
	std::vector<VMDLight> lights;
	std::vector<VMDSelfShadow> selfShadowData;

	std::string strModelPath = filepath;
	FILE* fp;
	fopen_s(&fp, strModelPath.c_str(), "rb");
	if (fp == nullptr) return false;

	fseek(fp, 50, SEEK_SET); // 50バイト飛ばす

	unsigned int motionDataNum = 0;
	fread(&motionDataNum, sizeof(motionDataNum), 1, fp);
	vmdMotionData_Raw.resize(motionDataNum);
	fread(vmdMotionData_Raw.data(), vmdMotionData_Raw.size() * sizeof(VMDMotion_Raw), 1, fp);

	uint32_t morphCount = 0;
	fread(&morphCount, sizeof(morphCount), 1, fp);
	morphs.resize(morphCount);
	fread(morphs.data(), sizeof(VMDMorph), morphCount, fp);

	uint32_t vmdCameraCount = 0;
	fread(&vmdCameraCount, sizeof(vmdCameraCount), 1, fp);
	cameraData.resize(vmdCameraCount);
	fread(cameraData.data(), sizeof(VMDCamera), vmdCameraCount, fp);

	uint32_t vmdLightCount = 0;
	fread(&vmdLightCount, sizeof(vmdLightCount), 1, fp);
	lights.resize(vmdLightCount);
	fread(lights.data(), sizeof(VMDLight), vmdLightCount, fp);

	uint32_t selfShadowCount = 0;
	fread(&selfShadowCount, sizeof(selfShadowCount), 1, fp);
	selfShadowData.resize(selfShadowCount);
	fread(selfShadowData.data(), sizeof(VMDSelfShadow), selfShadowCount, fp);

	uint32_t ikSwitchCount = 0;
	fread(&ikSwitchCount, sizeof(ikSwitchCount), 1, fp);
	_ikEnableData.resize(ikSwitchCount);
	for (auto& ikEnable : _ikEnableData)
	{
		// キーフレーム情報なのでまずはフレーム番号を読み込み
		fread(&ikEnable.frameNo, sizeof(ikEnable.frameNo), 1, fp);

		// 可視フラグ
		uint8_t visibleFlg = 0;
		fread(&visibleFlg, sizeof(visibleFlg), 1, fp);

		// 対象ボーン数読み込み
		uint32_t ikBoneCount = 0;
		fread(&ikBoneCount, sizeof(ikBoneCount), 1, fp);

		for (int i = 0; i < ikBoneCount; ++i)
		{
			char ikBoneName[20];
			fread(ikBoneName, _countof(ikBoneName), 1, fp);

			uint8_t flg = 0;
			fread(&flg, sizeof(flg), 1, fp);
			ikEnable.ikEnableTable[ikBoneName] = flg;
		}
	}


	fclose(fp);

	for (auto& vmdMotion : vmdMotionData_Raw) {
		_motiondata[std::string(vmdMotion.boneName, strnlen(vmdMotion.boneName, 15))].emplace_back(
			vmdMotion.frameNo, XMLoadFloat4(&vmdMotion.quaternion),
			vmdMotion.location,
			XMFLOAT2((float)vmdMotion.bezier[3] / 127.0f, (float)vmdMotion.bezier[7] / 127.0f),
			XMFLOAT2((float)vmdMotion.bezier[11] / 127.0f, (float)vmdMotion.bezier[15] / 127.0f)
		);

		// 総フレーム数獲得
		_duration = std::max<unsigned int>(_duration, vmdMotion.frameNo);

	}

	// モーションデータをソート
	for (auto& motion : _motiondata)
	{
		std::sort(
			motion.second.begin(), motion.second.end(),
			[](const KeyFrame& lval, const KeyFrame& rval)
			{
				return lval.frameNo < rval.frameNo;
			}
		);
	}


	return true;
}

void PMDActor::SolveCCDIK(const PMDIK& ik) {
	// ターゲット
	auto targetBoneNode = _boneNodeAddressArray[ik.boneIdx];
	auto targetOriginPos = XMLoadFloat3(&targetBoneNode->startPos);

	auto parentMat = _boneMatrices[_boneNodeAddressArray[ik.boneIdx]->ikParentBone];

	XMVECTOR det;
	auto invParentMat = XMMatrixInverse(&det, parentMat);
	auto targetNextPos = XMVector3Transform(targetOriginPos, _boneMatrices[ik.boneIdx] * invParentMat);
	
	// 末端ノード
	auto endPos = XMLoadFloat3(&_boneNodeAddressArray[ik.targetIdx]->startPos);

	// 中間ノード(ルートを含む)
	vector<XMVECTOR> bonePositions;
	for (auto& cidx : ik.nodeIdxes)
	{
		bonePositions.push_back(XMLoadFloat3(&_boneNodeAddressArray[cidx]->startPos));
	}

	std::vector<XMMATRIX> mats(bonePositions.size());
	fill(mats.begin(), mats.end(), XMMatrixIdentity());

	auto ikLimit = ik.limit * XM_PI;

	float epsilon = 0.1f;
	// ik に設定されている試行回数だけ繰り返す
	for (int c = 0; c < ik.iterations; ++c)
	{
		// ターゲットと末端がほぼ一致したら抜ける
		if (XMVector3Length(XMVectorSubtract(endPos, targetNextPos)).m128_f32[0] <= epsilon) {
			break;
		}

		// それぞれのボーンをさかのぼりながら
		// 角度制限に引っかからないように曲げていく

		// bonePositionsは、CCD-IKにおける各ノードの座標をベクタ配列にしたもの
		for (int bidx = 0; bidx < bonePositions.size(); ++bidx)
		{
			const auto& pos = bonePositions[bidx];
			// 対象ノードから末端ノードまでと
			// 対象ノードからターゲットまでのベクトル作成
			auto vecToEnd = XMVectorSubtract(endPos, pos);
			auto vecToTarget = XMVectorSubtract(targetNextPos, pos);

			// 両方正規化
			vecToEnd = XMVector3Normalize(vecToEnd);
			vecToTarget = XMVector3Normalize(vecToTarget);

			// ほぼ同じベクトルになってしまった場合は外積できないため次のボーンに引き渡す
			if (XMVector3Length(XMVectorSubtract(vecToEnd, vecToTarget)).m128_f32[0] <= epsilon)
			{
				continue;
			}

			// 外積計算および角度計算
			auto cross = XMVector3Normalize(XMVector3Cross(vecToEnd, vecToTarget)); // 軸になる
			
			// 便利な関数だが中身は cos(内積値) なので 0 ` 90 と 0 ` -90 の区別がない
			float angle = XMVector3AngleBetweenVectors(vecToEnd, vecToTarget).m128_f32[0];

			// 回転限界を越えてしまったときは限界値に補正
			angle = min(angle, ikLimit);
			XMMATRIX rot = XMMatrixRotationAxis(cross, angle); // 回転行列作成

			// 原点中心ではなく pos 中心に回転
			auto mat = XMMatrixTranslationFromVector(-pos)
				* rot
				* XMMatrixTranslationFromVector(pos);

			// 回転行列を保持しておく
			mats[bidx] *= mat;

			// 対象となる点をすべて回転させる（）乗算で回転重ね掛けを作っておく
			// なお、自分を回転させない
			for (auto idx = bidx - 1; idx >= 0; --idx)
			{
				bonePositions[idx] = XMVector3Transform(bonePositions[idx], mat);
			}

			endPos = XMVector3Transform(endPos, mat);

			// もし正解に近くなっていたらループを抜ける
			if (XMVector3Length(XMVectorSubtract(endPos, targetNextPos)).m128_f32[0] <= epsilon)
			{
				break;
			}
		}
	}

	int idx = 0;
	for (auto& cidx : ik.nodeIdxes)
	{
		_boneMatrices[cidx] = mats[idx];
		++idx;
	}

	auto rootNode = _boneNodeAddressArray[ik.nodeIdxes.back()];
	RecursiveMatrixMultiply(rootNode, parentMat);
};

void PMDActor::SolveCosineIK(const PMDIK& ik) {
	// IK 構成点を保存
	std::vector<XMVECTOR> positions;

	// IK のそれぞれのボーン間の距離を保存
	std::array<float, 2> edgeLens;

	// ターゲット（末端ボーンではなく、末端ボーンが近づく目標ボーンの座標を取得）
	auto& targetNode = _boneNodeAddressArray[ik.boneIdx];
	auto targetPos = XMVector3Transform(
		XMLoadFloat3(&targetNode->startPos),
		_boneMatrices[ik.boneIdx]
	);

	// IK チェーンが逆順なので、逆に並ぶようにする

	// 末端ボーン
	auto endNode = _boneNodeAddressArray[ik.boneIdx];
	positions.emplace_back(XMLoadFloat3(&endNode->startPos));

	// 中間およびルートボーン
	for (auto& chainBoneIdx : ik.nodeIdxes)
	{
		auto boneNode = _boneNodeAddressArray[chainBoneIdx];
		positions.emplace_back(XMLoadFloat3(&boneNode->startPos));
	}

	// 分かりづらいので逆にする
	reverse(positions.begin(), positions.end());

	// 元の長さを測っておく
	edgeLens[0] = XMVector3Length(XMVectorSubtract(positions[1], positions[0])).m128_f32[0];
	edgeLens[1] = XMVector3Length(XMVectorSubtract(positions[2], positions[1])).m128_f32[0];

	// ルートボーン座標変換（逆順になっているため使用するインデックスに注意）
	positions[0] = XMVector3Transform(positions[0], _boneMatrices[ik.nodeIdxes[1]]);
	
	// 真ん中は自動計算されるので計算しない

	// 先端ボーン
	positions[2] = XMVector3Transform(positions[2], _boneMatrices[ik.boneIdx]);

	// ルートから先端へのベクトルを作っておく
	auto linearVec = XMVectorSubtract(positions[2], positions[0]);

	float A = XMVector3Length(linearVec).m128_f32[0];
	float B = edgeLens[0];
	float C = edgeLens[1];

	linearVec = XMVector3Normalize(linearVec);

	// ルートから真ん中への角度計算
	float theta1 = acosf((A * A + B * B - C * C) / (2 * A * B));

	// 真ん中からターゲットへの角度計算
	float theta2 = acosf((B * B + C * C - A * A) / (2 * B * C));

	// 軸を求める
	// もし真ん中が「ひざ」であれば、強制的にX軸とする
	XMVECTOR axis;
	if (find(_kneeIdxes.begin(), _kneeIdxes.end(), ik.nodeIdxes[0]) == _kneeIdxes.end())
	{
		auto vm = XMVector3Normalize(XMVectorSubtract(positions[2], positions[0]));
		auto vt = XMVector3Normalize(XMVectorSubtract(targetPos, positions[0]));
		axis = XMVector3Cross(vt, vm);
	}
	else
	{
		auto right = XMFLOAT3(1, 0, 0);
		axis = XMLoadFloat3(&right);
	}

	// 注意点：IKチェーンはルートに向かってから数えられるため１がルートに近い
	auto mat1 = XMMatrixTranslationFromVector(-positions[0]);
	mat1 *= XMMatrixRotationAxis(axis, theta1);
	mat1 *= XMMatrixTranslationFromVector(positions[0]);


	auto mat2 = XMMatrixTranslationFromVector(-positions[1]);
	mat2 *= XMMatrixRotationAxis(axis, theta2 - XM_PI);
	mat2 *= XMMatrixTranslationFromVector(positions[1]);

	_boneMatrices[ik.nodeIdxes[1]] *= mat1;
	_boneMatrices[ik.nodeIdxes[0]] = mat2 * _boneMatrices[ik.nodeIdxes[1]];
	_boneMatrices[ik.targetIdx] = _boneMatrices[ik.nodeIdxes[0]];
};

float PMDActor::GetYFromXOnBezier(
	float x, const XMFLOAT2& a, const XMFLOAT2& b, uint8_t n
) {
	if (a.x == a.y && b.x == b.y)
	{
		return x; // 計算不要
	}

	float t = x;
	const float k0 = 1 + 3 * a.x - 3 * b.x; // t^3の係数
	const float k1 = 3 * b.x - 6 * a.x;		// t^2の係数
	const float k2 = 3 * a.x;				// tの係数

	// 誤差の範囲内かどうかに使用する定数
	constexpr float epsilon = 0.0005f;

	// tを近似で求める
	for (int i = 0; i < n; ++i)
	{
		// f(t)を求める
		auto ft = k0 * t * t * t + k1 * t * t + k2 * t - x;

		// もし結果が0に近い（誤差の範囲内）なら打ち切る
		if (ft <= epsilon && ft >= -epsilon)
		{
			break;
		}

		t -= ft / 2; //刻む
	}

	// 求めたい t はすでに求めているので y を計算する
	auto r = 1 - t;
	return t * t * t + 3 * t * t * r * b.y + 3 * t * r * r * a.y;
}

void PMDActor::PlayAnimation() {
	DWORD elapsedTime = timeGetTime() - _startTime; // 経過時間

	// 1. 小数精度での現在のフレームを計算
	float currentFrame = (elapsedTime / 1000.0f) * 30.0f;
	// ループ
	if (static_cast<unsigned int>(currentFrame) > _duration) {
		_startTime = timeGetTime();
		currentFrame = 0.0f;
	}
	// 検索やループ判定用に整数化
	unsigned int frameNo = static_cast<unsigned int>(currentFrame);
	
	// 行列情報クリア
	std::fill(_boneMatrices.begin(), _boneMatrices.end(), XMMatrixIdentity());

	// モーションデータ更新
	for (auto& bonemotion : _motiondata)
	{
		auto itBoneNode = _boneNodeTable.find(bonemotion.first);
		if (itBoneNode == _boneNodeTable.end()) continue;

		auto& node = itBoneNode->second;

		// 合致するものを探す
		auto motions = bonemotion.second;
		auto rit = std::find_if(
			motions.rbegin(), motions.rend(),
			[frameNo](const KeyFrame& motion)
			{
				return motion.frameNo <= frameNo;
			}
		);

		// 合致するものがなければ飛ばす
		if (rit == motions.rend()) continue;

		XMMATRIX rotation;
		auto it = rit.base();


		// 2. 補間係数 t の計算をシンプル化し、ゼロ除算を防止
		if (it != motions.end() && it->frameNo > rit->frameNo)
		{
			// (現在の小数フレーム - 前のキーフレーム) / (次のキーフレーム - 前のキーフレーム)
			float t = (currentFrame - static_cast<float>(rit->frameNo))
				/ static_cast<float>(it->frameNo - rit->frameNo);
			t = std::clamp(t, 0.0f, 1.0f);
			t = GetYFromXOnBezier(t, it->p1, it->p2, 12);
			rotation = XMMatrixRotationQuaternion(XMQuaternionSlerp(rit->quaternion, it->quaternion, t));
		}
		else
		{
			rotation = XMMatrixRotationQuaternion(rit->quaternion);
		}

		auto& pos = node.startPos;
		auto mat = XMMatrixTranslation(-pos.x, -pos.y, -pos.z)
			* rotation	// 回転する
			* XMMatrixTranslation(pos.x, pos.y, pos.z);

		XMVECTOR offset = XMLoadFloat3(&rit->offset);
		// 次キーがあれば XMVectorLerp(offset, XMLoadFloat3(&it->offset), t)

		_boneMatrices[node.boneIdx] = mat * XMMatrixTranslationFromVector(offset);
	}

	RecursiveMatrixMultiply(&_boneNodeTable["センター"], XMMatrixIdentity());
	IKSolve(frameNo);
	std::copy(_boneMatrices.begin(), _boneMatrices.end(), _mappedTransform->bones.begin());
}

bool PMDActor::Load(const char* filepath) {
	// PMD
	char signature[4] = {}; // 先頭3バイトは文字列"pmd"
	PMDHeader pmdheader;
	unsigned int vertNum; // 頂点数
	constexpr size_t vert_raw_size = sizeof(PMDVertex_Raw); // 1頂点辺りのサイズ (38)
	constexpr size_t vert_gpu_size = sizeof(PMDVertex);     // パディング済み (40)
	std::vector<PMDVertex_Raw> rawVertices;// 受け取り用の38バイト頂点配列
	std::vector<PMDVertex> vertices;// GPU用の40バイト頂点配列
	std::vector<unsigned short> indices;
	unsigned int materialNum; // マテリアル数
	std::vector<PMDMaterial_Raw> rawPmdMaterials;
	unsigned short boneNum = 0;
	std::vector<PMDBone> pmdBones;
	uint16_t ikNum = 0;

	std::string strModelPath = filepath;
	FILE* fp;
	fopen_s(&fp, strModelPath.c_str(), "rb");
	if (fp == nullptr) return false;

	fread(signature, 3, 1, fp);
	fread(&pmdheader, sizeof(pmdheader), 1, fp);

	fread(&vertNum, sizeof(vertNum), 1, fp); // 頂点数はヘッダーデータ直後
	rawVertices.resize(vertNum * vert_raw_size); // バッファーの確保
	fread(rawVertices.data(), rawVertices.size(), 1, fp);

	fread(&indicesNum, sizeof(indicesNum), 1, fp);
	indices.resize(indicesNum);
	fread(indices.data(), indices.size() * sizeof(unsigned short), 1, fp);

	fread(&materialNum, sizeof(materialNum), 1, fp);
	rawPmdMaterials.resize(materialNum);
	fread(rawPmdMaterials.data(), rawPmdMaterials.size() * sizeof(PMDMaterial_Raw), 1, fp);

	fread(&boneNum, sizeof(boneNum), 1, fp);
	pmdBones.resize(boneNum);
	fread(pmdBones.data(), sizeof(PMDBone), boneNum, fp);

	fread(&ikNum, sizeof(ikNum), 1, fp);
	_ikData.resize(ikNum);
	for (auto& ik : _ikData)
	{
		fread(&ik.boneIdx, sizeof(ik.boneIdx), 1, fp);
		fread(&ik.targetIdx, sizeof(ik.targetIdx), 1, fp);

		uint8_t chainLen = 0;	// 間にいくつノードがあるか
		fread(&chainLen, sizeof(chainLen), 1, fp);
		fread(&ik.iterations, sizeof(ik.iterations), 1, fp);
		fread(&ik.limit, sizeof(ik.limit), 1, fp);

		// 間のノード数があれば
		if (chainLen > 0)
		{
			ik.nodeIdxes.resize(chainLen);
			fread(ik.nodeIdxes.data(), sizeof(ik.nodeIdxes[0]), chainLen, fp);
		}
	}

	fclose(fp);

	// 入力レイアウトに R32G32B32_FLOAT（4バイト単位の型）が入っているので、
	// ストライドも 4 の倍数でなければいけない
	vertices.resize(vertNum);
	for (unsigned i = 0; i < vertNum; ++i) {
		// 必要な38バイト分だけコピー（残りのpaddingは0初期化される）
		memcpy(&vertices[i], &rawVertices[i], vert_raw_size);
	}

	materials.resize(rawPmdMaterials.size());
	for (unsigned i = 0; i < rawPmdMaterials.size(); ++i) {
		materials[i].indicesNum = rawPmdMaterials[i].indicesNum;
		materials[i].material.diffuse = rawPmdMaterials[i].diffuse;
		materials[i].material.alpha = rawPmdMaterials[i].alpha;
		materials[i].material.specular = rawPmdMaterials[i].specular;
		materials[i].material.specularity = rawPmdMaterials[i].specularity;
		materials[i].material.ambient = rawPmdMaterials[i].ambient;
		//materials[i].additional
	}

	// テクスチャパス
	vector<string> texturePath(materialNum, "");
	vector<string> sphPath(materialNum, "");
	vector<string> spaPath(materialNum, "");
	vector<string> toonPath(materialNum, "");
	// --- ループ内の処理 ---
	for (size_t i = 0; i < materialNum; ++i)
	{
		if (strlen(rawPmdMaterials[i].texFilePath) == 0) continue;

		// '*' で文字列を分割しながら順次処理
		std::stringstream ss(rawPmdMaterials[i].texFilePath);
		std::string fileName;

		while (std::getline(ss, fileName, '*'))
		{
			if (fileName.empty()) continue;

			auto ext = GetLowerExt(fileName);
			auto fullPath = GetTexturePathFromModelAndTexPath(strModelPath, fileName.c_str());

			// 拡張子に応じて格納先を振り分け
			if (ext == ".sph") {
				_resourceTable[fullPath] = LoadTextureFromFile(fullPath);
				sphPath[i] = fullPath;
			}
			else if (ext == ".spa") {
				_resourceTable[fullPath] = LoadTextureFromFile(fullPath);
				spaPath[i] = fullPath;
			}
			else {
				_resourceTable[fullPath] = LoadTextureFromFile(fullPath);
				texturePath[i] = fullPath;
			}
		}
	}

	for (int i = 0; i < materialNum; ++i)
	{
		string toonFilePath = "toon/";

		char toonFileName[16];

		sprintf_s(toonFileName, "toon%02d.bmp", rawPmdMaterials[i].toonIdx + 1);

		toonFilePath += toonFileName;

		_resourceTable[toonFilePath] = LoadTextureFromFile(toonFilePath);
		toonPath[i] = toonFilePath;
	}

	// インデックスと名前の対応関係構築のためにあとで使う
	std::vector<std::string> boneNames(pmdBones.size());

	_boneNameArray.resize(pmdBones.size());
	_boneNodeAddressArray.resize(pmdBones.size());

	_kneeIdxes.clear();
	for (int idx = 0; idx < pmdBones.size(); ++idx) {
		// ボーンノードマップを作る
		auto& pb = pmdBones[idx];
		boneNames[idx] = pb.boneName;
		auto& node = _boneNodeTable[pb.boneName];
		node.boneIdx = idx;
		node.startPos = pb.pos;
		node.boneType = pb.type;
		node.ikParentBone = pb.parentNo;

		// インデックス検索がしやすいように
		_boneNameArray[idx] = pb.boneName;
		_boneNodeAddressArray[idx] = &node;


		std::string boneName = pb.boneName;
		if (boneName.find("ひざ") != std::string::npos)
		{
			_kneeIdxes.emplace_back(idx);
		}
	}
	// 親子関係を構築する
	for (auto& pb : pmdBones)
	{
		// 親インデックスをチェック（あり得ない番号なら飛ばす）
		if (pb.parentNo >= pmdBones.size())
		{
			continue;
		}
		auto parentName = boneNames[pb.parentNo];
		_boneNodeTable[parentName].children.emplace_back(
			&_boneNodeTable[pb.boneName]
		);
	}
	_boneMatrices.resize(pmdBones.size());

	// ボーンを全て初期化する
	std::fill(
		_boneMatrices.begin(),
		_boneMatrices.end(),
		XMMatrixIdentity()
	);

	_vertBuff = _dx12.CreateBuffer(vertices.size() * vert_gpu_size, vertices.data());
	_idxBuff = _dx12.CreateBuffer(indices.size() * sizeof(unsigned short), indices.data());

	// 頂点バッファービュー
	vbView.BufferLocation = _vertBuff->GetGPUVirtualAddress(); // バッファーの仮想アドレス
	vbView.SizeInBytes = vertices.size() * vert_gpu_size;	// 全バイト数
	vbView.StrideInBytes = vert_gpu_size;	// 一頂点辺りのバイト数

	// インデックスバッファービューを作成
	ibView.BufferLocation = _idxBuff->GetGPUVirtualAddress();
	ibView.Format = DXGI_FORMAT_R16_UINT;
	ibView.SizeInBytes = indices.size() * sizeof(indices[0]);


	XMMATRIX matrix = XMMatrixIdentity();

	// 1. 定数バッファの作成して中身をマップで書き換える
	size_t cbSize = (sizeof(Transform) + 255) & ~255; // 256バイトアライメント

	// 定数バッファ
	auto heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto resdesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

	HRESULT hr = _dx12.Device()->CreateCommittedResource(
		&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_transformBuff )
	);
	if (FAILED(hr)) return false;

	// CPUから読み込まないことを明確にするため Range(0, 0) を指定
	CD3DX12_RANGE readRange(0, 0);
	hr = _transformBuff ->Map(0, &readRange, (void**)&_mappedTransform );

	if (FAILED(hr)) return false;


	// データのコピー
	_mappedTransform->world = _worldMatrix;
	assert(pmdBones.size() <= 256);
	std::copy(_boneMatrices.begin(), _boneMatrices.end(), _mappedTransform->bones.begin());

	// マテリアル用バッファー
	auto materialBuffSize = sizeof(MaterialForHlsl);
	materialBuffSize = (materialBuffSize + 0xff) & ~0xff; // 複数のマテリアルを切り替えるために256アライメントをマテリアルごとに行う
	// 無駄な領域を作らない方法として DrawIndexedInstancedを呼ぶ旅に CopyBufferRegion で転送する、まとめて1Dテクスチャデータとして転送してマテリアルIDで参照位置を変える、などがある
	auto heappropmat = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto resdescmat = CD3DX12_RESOURCE_DESC::Buffer(materialBuffSize * materialNum); // もったいないが今回はこうする
	HRESULT result = _dx12.Device()->CreateCommittedResource(
		&heappropmat,
		D3D12_HEAP_FLAG_NONE,
		&resdescmat,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_materialBuff)
	);
	if (FAILED(result)) return false;
	// マップマテリアルにコピー
	char* mapMaterial = nullptr;
	result = _materialBuff->Map(0, nullptr, (void**)&mapMaterial);
	for (auto& m : materials) {// 強引なキャストだが今はこうする
		*((MaterialForHlsl*)mapMaterial) = m.material; // データコピー
		mapMaterial += materialBuffSize; // 次のアライメント位置まで進める（256の倍数）
	}
	_materialBuff->Unmap(0, nullptr);

	// ビューの作成
	D3D12_SHADER_RESOURCE_VIEW_DESC matSRVDesc = {};
	matSRVDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // デフォルト
	matSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	matSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	matSRVDesc.Texture2D.MipLevels = 1;	// ミップマップは使用しないので1

	D3D12_CONSTANT_BUFFER_VIEW_DESC matCBVDesc = {};
	matCBVDesc.BufferLocation = _materialBuff->GetGPUVirtualAddress(); // バッファーアドレス
	matCBVDesc.SizeInBytes = materialBuffSize; // マテリアルの 256 アライメントサイズ

	// ディスクリプタヒープに追加する
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descHeapDesc.NodeMask = 0;
	descHeapDesc.NumDescriptors = materialNum * MATERIAL_MULTIPLIER; // マテリアルと拡張テクスチャ数
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	result = _dx12.Device()->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&_basicDescHeap));
	if (FAILED(result)) return false;

	// ディスクリプタの先頭ハンドルを取得しておく
	auto basicHeapHandle = _basicDescHeap->GetCPUDescriptorHandleForHeapStart();

	auto inc = _dx12.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_whiteTex = _dx12.CreateSolidColorTexture(0xff, 0xff, 0xff);
	_blackTex = _dx12.CreateSolidColorTexture(0, 0, 0);

	// トゥーン用デフォルトグラデーションテクスチャ
	{
		std::vector<unsigned int> data(4 * 256);
		auto it = data.begin();
		unsigned int c = 0xff;
		for (; it != data.end(); it += 4) {
			auto col = (c << 0xff) | (c << 16) | (c << 8) | c;
			std::fill(it, it + 4, col);
			--c;
		}
		_grayGradationTex = _dx12.CreateTextureFromData(
			4, 256, DXGI_FORMAT_R8G8B8A8_UNORM, 
			data.data(), 4 * sizeof(unsigned int), sizeof(unsigned int) * data.size()
		);
	}
	for (int i = 0; i < materialNum; ++i) {
		// マテリアル用定数バッファービュー
		_dx12.Device()->CreateConstantBufferView(&matCBVDesc, basicHeapHandle);
		basicHeapHandle.ptr += inc;
		matCBVDesc.BufferLocation += materialBuffSize;

		// シェーダーリソースビュー
		if (texturePath[i].empty())
		{
			matSRVDesc.Format = _whiteTex->GetDesc().Format;
			_dx12.Device()->CreateShaderResourceView(_whiteTex.Get(), &matSRVDesc, basicHeapHandle);
		}
		else
		{
			matSRVDesc.Format = _resourceTable[texturePath[i]]->GetDesc().Format;
			_dx12.Device()->CreateShaderResourceView(_resourceTable[texturePath[i]].Get(), &matSRVDesc, basicHeapHandle);
		}
		basicHeapHandle.ptr += inc;

		if (sphPath[i].empty())
		{
			matSRVDesc.Format = _whiteTex->GetDesc().Format;
			_dx12.Device()->CreateShaderResourceView(_whiteTex.Get(), &matSRVDesc, basicHeapHandle);
		}
		else
		{
			matSRVDesc.Format = _resourceTable[sphPath[i]]->GetDesc().Format;
			_dx12.Device()->CreateShaderResourceView(_resourceTable[sphPath[i]].Get(), &matSRVDesc, basicHeapHandle);
		}
		basicHeapHandle.ptr += inc;

		if (spaPath[i].empty())
		{
			matSRVDesc.Format = _blackTex->GetDesc().Format;
			_dx12.Device()->CreateShaderResourceView(_blackTex.Get(), &matSRVDesc, basicHeapHandle);
		}
		else
		{
			matSRVDesc.Format = _resourceTable[spaPath[i]]->GetDesc().Format;
			_dx12.Device()->CreateShaderResourceView(_resourceTable[spaPath[i]].Get(), &matSRVDesc, basicHeapHandle);
		}
		basicHeapHandle.ptr += inc;

		if (toonPath[i].empty())
		{
			matSRVDesc.Format = _grayGradationTex->GetDesc().Format;
			_dx12.Device()->CreateShaderResourceView(_grayGradationTex.Get(), &matSRVDesc, basicHeapHandle);
		}
		else
		{
			matSRVDesc.Format = _resourceTable[toonPath[i]]->GetDesc().Format;
			_dx12.Device()->CreateShaderResourceView(_resourceTable[toonPath[i]].Get(), &matSRVDesc, basicHeapHandle);
		}
		basicHeapHandle.ptr += inc;
	}

	_startTime = timeGetTime();
	return true;
};
void PMDActor::Update() {
	PlayAnimation();
	angle += 0.01f;
	_worldMatrix = XMMatrixRotationY(angle);
	_mappedTransform->world = _worldMatrix;
};
void PMDActor::Draw() {
	// ========= 実際の描画 =========
	auto cmdList = _dx12.CommandList();

	// ワールド行列（b2）をルートCBVで直接渡す
	cmdList->SetGraphicsRootConstantBufferView(2, _transformBuff->GetGPUVirtualAddress());

	// テクスチャCBV（ヒープ）のセット
	ID3D12DescriptorHeap* ppHeaps[] = { _basicDescHeap.Get() };
	cmdList->SetDescriptorHeaps(1, ppHeaps);
	auto descHeapH = _basicDescHeap->GetGPUDescriptorHandleForHeapStart();

	// ジオメトリのセットと描画
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->IASetVertexBuffers(0, 1, &vbView);
	cmdList->IASetIndexBuffer(&ibView);

	auto cbvsrvIncSize = _dx12.Device()->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
	) * MATERIAL_MULTIPLIER;
	unsigned int idxOffset = 0;
	for (auto& m : materials) {
		cmdList->SetGraphicsRootDescriptorTable(1, descHeapH);
		cmdList->DrawIndexedInstanced(m.indicesNum, 1, idxOffset, 0, 0);
		// ヒープポインターとインデックスを次に進める
		descHeapH.ptr += cbvsrvIncSize;
		idxOffset += m.indicesNum;
	}
};

void PMDActor::RecursiveMatrixMultiply(
	const BoneNode* node, const DirectX::XMMATRIX& mat
)
{
	_boneMatrices[node->boneIdx] *= mat;

	for (const auto& cnode : node->children)
	{
		RecursiveMatrixMultiply(cnode, _boneMatrices[node->boneIdx]);
	}
}