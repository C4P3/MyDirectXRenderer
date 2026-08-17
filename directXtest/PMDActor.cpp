#include "PMDActor.h"
#define MATERIAL_MULTIPLIER 5

#include <Windows.h>
#include <tchar.h> // _T マクロ用
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <wrl/client.h> // ComPtr用
#include <string>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <DirectXTex.h>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <span>
#include <map>

#include "d3dx12.h"
#include "Application.h"
#include "Dx12Wrapper.h"
#include "PMDRenderer.h"
#include "PMDActor.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "DirectXTex.lib")
#pragma comment(lib, "dxguid.lib")

#ifdef _DEBUG
#include <iostream>
#include <string_view>
#endif

using namespace std;
using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace fs = std::filesystem;

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

	std::string strModelPath = "Model/初音ミク.pmd";
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

	// 1. 定数バッファの作成して中身をマップで書き換える（バッファサイズ: 256バイト、コピー元サイズ: sizeof(matrix) = 64バイト）
	size_t cbSize = (sizeof(SceneMatrix) + 255) & ~255; // 256バイトアライメント

	// 定数バッファ
	auto heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto resdesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

	HRESULT hr = _dx12.Device()->CreateCommittedResource(
		&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_constBuff)
	);
	if (FAILED(hr)) return false;

	// CPUから読み込まないことを明確にするため Range(0, 0) を指定
	CD3DX12_RANGE readRange(0, 0);
	hr = _constBuff->Map(0, &readRange, (void**)&_mapMatrix);

	if (FAILED(hr)) return false;

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
	descHeapDesc.NumDescriptors = 1 + materialNum * MATERIAL_MULTIPLIER; // 先頭に WVP 用 CBV + マテリアル数4つ分
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	result = _dx12.Device()->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&_basicDescHeap));
	if (FAILED(result)) return false;

	// ディスクリプタの先頭ハンドルを取得しておく
	auto basicHeapHandle = _basicDescHeap->GetCPUDescriptorHandleForHeapStart();

	// CBV
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = _constBuff->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = static_cast<UINT>(_constBuff->GetDesc().Width);
	_dx12.Device()->CreateConstantBufferView(&cbvDesc, basicHeapHandle);
	// 次の場所に移動する
	basicHeapHandle.ptr += _dx12.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


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
	return true;
};
void PMDActor::Update(PMDRenderer& renderer, const XMMATRIX& view, const XMMATRIX& proj) {
	angle += 0.01f;
	_worldMatrix = XMMatrixRotationY(angle);
	_mapMatrix->world = _worldMatrix;
	_mapMatrix->view = view;
	_mapMatrix->proj = proj;
	// ========= 実際の描画 =========
	renderer.Draw(vbView, ibView, _basicDescHeap.Get(), materials);
};
void PMDActor::Draw() {
};