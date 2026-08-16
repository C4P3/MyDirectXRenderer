
#define MATERIAL_MULTIPLIER 4

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

// 頂点データ構造体
struct Vertex
{
	XMFLOAT3 pos;
	XMFLOAT2 uv;
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
	XMMATRIX world;	
	XMMATRIX view;
	XMMATRIX proj;
	XMMATRIX eye;	// 視点座標
};

#pragma pack(push, 1) // 1バイト境界に設定（パディングを無効化）
struct PMDVertex_Raw
{
	XMFLOAT3 pos;			// 12バイト
	XMFLOAT3 normal;		// 12バイト
	XMFLOAT2 uv;			// 8バイト
	unsigned short boneNo[2];	// 4バイト
	unsigned char boneWeight;	// 1バイト
	unsigned char edgeFlg;		// 1バイト
}; // これで確実に sizeof(PMDVertex_Raw) == 38 になる

// PMD マテリアル構造体
struct PMDMaterial_Raw
{
	XMFLOAT3 diffuse;	// ディフューズ色
	float alpha;	// ディフューズα
	float specularity;	// スペキュラの強さ（乗算値）
	XMFLOAT3 specular;	// スペキュラ色
	XMFLOAT3 ambient;	// アンビエント色
	unsigned char toonIdx;	// トゥーン番号
	unsigned char edgeFlg;	// マテリアルごとの輪郭線フラグ
	// pragma pack(1) によりここに2バイトパディングが発生しない
	unsigned int indicesNum;	// このマテリアルが割り当てられるインデックス数
	char texFilePath[20];	// テクスチャファイルパス
}; // 70バイト
#pragma pack(pop) // 元のアライメント設定に戻す

struct PMDVertex
{
	XMFLOAT3 pos;				// 頂点座標		: 12バイト
	XMFLOAT3 normal;			// 法線ベクトル	: 12バイト
	XMFLOAT2 uv;				// uv座標		: 8バイト
	unsigned short boneNo[2];	// ボーン番号	: 4バイト
	unsigned char boneWeight;	// ボーン影響度 : 1バイト
	unsigned char edgeFlg;		// 輪郭線フラグ	: 1バイト
	unsigned char padding[2];	// 明示的に2バイト埋める (合計40バイト)
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

ComPtr<ID3D12Resource> CreateTextureFromData(
	ID3D12Device* dev,
	UINT64 width,
	UINT height,
	DXGI_FORMAT format,
	const void* pixels,
	size_t rowPitch,
	size_t slicePitch
)
{
	D3D12_HEAP_PROPERTIES texHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0);

	D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		format, width, height, 1, 1 // arraySize = 1, mipLevels = 1
	);

	// バッファー作成
	ComPtr<ID3D12Resource> texBuff = nullptr;
	auto result = dev->CreateCommittedResource(
		&texHeapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&texBuff)
	);
	if (FAILED(result)) return nullptr;

	result = texBuff->WriteToSubresource(
		0, nullptr, pixels,
		static_cast<UINT>(rowPitch),
		static_cast<UINT>(slicePitch)
	);
	if (FAILED(result)) return nullptr;

	return texBuff;
}

ComPtr<ID3D12Resource> LoadTextureFromFile(
	ID3D12Device* dev,
	const std::string& filePath
)
{
	// 1. 拡張子を取得して小文字化
	std::string ext = GetLowerExt(filePath);

	// 2. ワイド文字列に変換
	std::wstring wFilePath = GetWideStringFromString(filePath);

	TexMetadata metadata = {};
	ScratchImage scratchImg = {};
	HRESULT hr = S_OK;

	// 3. 拡張子に応じて DirectXTex のロード関数を呼び分ける（ここに処理を隠蔽！）
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
	return CreateTextureFromData(
		dev,
		metadata.width,
		metadata.height,
		metadata.format,
		img->pixels,
		img->rowPitch,
		img->slicePitch
	);
}

ComPtr<ID3D12Resource> CreateSolidColorTexture(
	ID3D12Device* dev, 
	uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255
)
{
	uint8_t data[4 * 4 * 4];
	for (size_t i = 0; i < sizeof(data); i += 4) {
		data[i + 0] = r;
		data[i + 1] = g;
		data[i + 2] = b;
		data[i + 3] = a;
	}

	return CreateTextureFromData(
		dev,
		4,
		4,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		data,
		4 * 4,	// 1ラインサイズ
		sizeof(data)	// 全サイズ
	);
}

#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif

#pragma region 1. ウィンドウの生成
	Application app(1280, 720);
	if (!app.Init()) {
		return -1;
	}

	int window_width = app.GetWindowWidth();
	int window_height = app.GetWindowHeight();
	HWND hwnd = app.GetWindowHandle();
#pragma endregion ウィンドウの生成

	Dx12Wrapper dx12;
	if (!dx12.Init(app.GetWindowHandle(), app.GetWindowWidth(), app.GetWindowHeight())) {
		return -1;
	}

	HRESULT result;
#pragma region 3. パイプラインの構築
	PMDRenderer renderer;
	if (!renderer.Init(dx12)) return -1; // パイプライン構築
#pragma endregion 3. パイプラインの構築

#pragma region 4. アセットの作成とデータ転送


	// PMD
	char signature[4] = {}; // 先頭3バイトは文字列"pmd"
	PMDHeader pmdheader;
	unsigned int vertNum; // 頂点数
	constexpr size_t vert_raw_size = sizeof(PMDVertex_Raw); // 1頂点辺りのサイズ (38)
	constexpr size_t vert_gpu_size = sizeof(PMDVertex);     // パディング済み (40)
	std::vector<PMDVertex_Raw> rawVertices;// 受け取り用の38バイト頂点配列
	std::vector<PMDVertex> vertices;// GPU用の40バイト頂点配列
	std::vector<unsigned short> indices;
	unsigned int indicesNum; // インデックス数
	unsigned int materialNum; // マテリアル数
	std::vector<PMDMaterial_Raw> rawPmdMaterials;
	std::vector<Material> materials;
	
	std::string strModelPath = "Model/初音ミク.pmd";
	FILE* fp;
	fopen_s(&fp, strModelPath.c_str(), "rb");
	if (fp == nullptr) return -1;
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

	// ファイル名パスとリソースのマップテーブル
	std::map<std::string, ComPtr<ID3D12Resource>> _resourceTable;
	// テクスチャパス
	std::vector<ComPtr<ID3D12Resource>> textureResources(materialNum);
	std::vector<ComPtr<ID3D12Resource>> sphResources(materialNum);
	std::vector<ComPtr<ID3D12Resource>> spaResources(materialNum);
	// --- ループ内の処理 ---
	for (size_t i = 0; i < rawPmdMaterials.size(); ++i)
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
				sphResources[i] = LoadTextureFromFile(dx12.Device(), fullPath);
			}
			else if (ext == ".spa") {
				spaResources[i] = LoadTextureFromFile(dx12.Device(), fullPath);
			}
			else {
				textureResources[i] = LoadTextureFromFile(dx12.Device(), fullPath);
			}
		}
	}

	ComPtr<ID3D12Resource> vertBuff = dx12.CreateBuffer(vertices.size() * vert_gpu_size, vertices.data());
	ComPtr<ID3D12Resource> idxBuff = dx12.CreateBuffer(indices.size() * sizeof(unsigned short), indices.data());

	// 頂点バッファービュー
	D3D12_VERTEX_BUFFER_VIEW vbView = {};
	vbView.BufferLocation = vertBuff->GetGPUVirtualAddress(); // バッファーの仮想アドレス
	vbView.SizeInBytes = vertices.size() * vert_gpu_size;	// 全バイト数
	vbView.StrideInBytes = vert_gpu_size;	// 一頂点辺りのバイト数

	// インデックスバッファービューを作成
	D3D12_INDEX_BUFFER_VIEW ibView = {};
	ibView.BufferLocation = idxBuff->GetGPUVirtualAddress();
	ibView.Format = DXGI_FORMAT_R16_UINT;
	ibView.SizeInBytes = indices.size() * sizeof(indices[0]);


	XMMATRIX matrix = XMMatrixIdentity();

	// ワールド行列
	// y軸中心に45度
	auto worldMat = XMMatrixRotationY(XM_PIDIV4);

	// ビュー行列
	XMFLOAT3 eye(0, 15, -15);
	XMFLOAT3 target(0, 10, 0);
	XMFLOAT3 up(0, 1, 0);
	auto viewMat = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&up));

	//プロジェクション行列
	auto projMat = XMMatrixPerspectiveFovLH(
		XM_PIDIV2,
		static_cast<float>(window_width) / static_cast<float>(window_height),
		1.0f, // 近いクリップ面距離
		100.0f // 遠いクリップ面距離
	);

	// 1. 定数バッファの作成して中身をマップで書き換える（バッファサイズ: 256バイト、コピー元サイズ: sizeof(matrix) = 64バイト）
	size_t cbSize = (sizeof(SceneMatrix) + 255) & ~255; // 256バイトアライメント

	// 定数バッファ
	ComPtr<ID3D12Resource> constBuff = nullptr;
	auto heapprop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto resdesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

	HRESULT hr = dx12.Device()->CreateCommittedResource(
		&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&constBuff)
	);
	if (FAILED(hr)) return -1;
	SceneMatrix* mapMatrix = nullptr;
	if (&matrix != nullptr) {
		// CPUから読み込まないことを明確にするため Range(0, 0) を指定
		CD3DX12_RANGE readRange(0, 0);
		hr = constBuff->Map(0, &readRange, (void**)&mapMatrix);

		if (SUCCEEDED(hr)) {
			// dataSize が指定されていなければ sizeInBytes を使用
			size_t copySize = (sizeof(matrix) > 0) ? sizeof(matrix) : cbSize;
			std::memcpy(mapMatrix, &matrix, copySize);

			// 書き込んだ範囲を指定して Unmap (nullptr でも可)
			//CD3DX12_RANGE writeRange(0, copySize);
			//constBuff->Unmap(0, &writeRange);
		}
	}

	// マテリアル用バッファー
	auto materialBuffSize = sizeof(MaterialForHlsl);
	materialBuffSize = (materialBuffSize + 0xff) & ~0xff; // 複数のマテリアルを切り替えるために256アライメントをマテリアルごとに行う
	// 無駄な領域を作らない方法として DrawIndexedInstancedを呼ぶ旅に CopyBufferRegion で転送する、まとめて1Dテクスチャデータとして転送してマテリアルIDで参照位置を変える、などがある
	ComPtr<ID3D12Resource> materialBuff = nullptr;
	auto heappropmat = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto resdescmat = CD3DX12_RESOURCE_DESC::Buffer(materialBuffSize * materialNum); // もったいないが今回はこうする
	result = dx12.Device()->CreateCommittedResource(
		&heappropmat,
		D3D12_HEAP_FLAG_NONE,
		&resdescmat,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&materialBuff)
	);
	if (FAILED(result)) return -1;
	// マップマテリアルにコピー
	char* mapMaterial = nullptr;
	result = materialBuff->Map(0, nullptr, (void**)&mapMaterial);
	for (auto& m : materials) {// 強引なキャストだが今はこうする
		*((MaterialForHlsl*)mapMaterial) = m.material; // データコピー
		mapMaterial += materialBuffSize; // 次のアライメント位置まで進める（256の倍数）
	}
	materialBuff->Unmap(0, nullptr);

	// ビューの作成
	D3D12_SHADER_RESOURCE_VIEW_DESC matSRVDesc = {};
	matSRVDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // デフォルト
	matSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	matSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	matSRVDesc.Texture2D.MipLevels = 1;	// みっぷマップは使用しないので1

	D3D12_CONSTANT_BUFFER_VIEW_DESC matCBVDesc = {};
	matCBVDesc.BufferLocation = materialBuff->GetGPUVirtualAddress(); // バッファーアドレス
	matCBVDesc.SizeInBytes = materialBuffSize; // マテリアルの 256 アライメントサイズ
	
	// ディスクリプタヒープに追加する
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descHeapDesc.NodeMask = 0;
	descHeapDesc.NumDescriptors = 1 + materialNum * MATERIAL_MULTIPLIER; // 先頭に WVP 用 CBV + マテリアル数4つ分
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	ComPtr<ID3D12DescriptorHeap> basicDescHeap = nullptr;
	result = dx12.Device()->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&basicDescHeap));
	if (FAILED(result)) return -1;

	// ディスクリプタの先頭ハンドルを取得しておく
	auto basicHeapHandle = basicDescHeap->GetCPUDescriptorHandleForHeapStart();

	// CBV
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = constBuff->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = static_cast<UINT>(constBuff->GetDesc().Width);
	dx12.Device()->CreateConstantBufferView(&cbvDesc, basicHeapHandle);
	// 次の場所に移動する
	basicHeapHandle.ptr += dx12.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	auto inc = dx12.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	ComPtr<ID3D12Resource> whiteTex = CreateSolidColorTexture(dx12.Device(), 0xff, 0xff, 0xff);
	ComPtr<ID3D12Resource> blackTex = CreateSolidColorTexture(dx12.Device(), 0, 0, 0);
	for (int i = 0; i < materialNum; ++i) {
		// マテリアル用定数バッファービュー
		dx12.Device()->CreateConstantBufferView(&matCBVDesc, basicHeapHandle);
		basicHeapHandle.ptr += inc;
		matCBVDesc.BufferLocation += materialBuffSize;

		// シェーダーリソースビュー
		if (textureResources[i] == nullptr) 
		{
			matSRVDesc.Format = whiteTex->GetDesc().Format;
			dx12.Device()->CreateShaderResourceView(whiteTex.Get(), &matSRVDesc, basicHeapHandle);
		}
		else
		{
			matSRVDesc.Format = textureResources[i]->GetDesc().Format;
			dx12.Device()->CreateShaderResourceView(textureResources[i].Get(), &matSRVDesc, basicHeapHandle);
		}
		basicHeapHandle.ptr += inc;

		if (sphResources[i] == nullptr)
		{
			matSRVDesc.Format = whiteTex->GetDesc().Format;
			dx12.Device()->CreateShaderResourceView(whiteTex.Get(), &matSRVDesc, basicHeapHandle);
		}
		else
		{
			matSRVDesc.Format = sphResources[i]->GetDesc().Format;
			dx12.Device()->CreateShaderResourceView(sphResources[i].Get(), &matSRVDesc, basicHeapHandle);
		}
		basicHeapHandle.ptr += inc;

		if (spaResources[i] == nullptr)
		{
			matSRVDesc.Format = blackTex->GetDesc().Format;
			dx12.Device()->CreateShaderResourceView(blackTex.Get(), &matSRVDesc, basicHeapHandle);
		}
		else
		{
			matSRVDesc.Format = spaResources[i]->GetDesc().Format;
			dx12.Device()->CreateShaderResourceView(spaResources[i].Get(), &matSRVDesc, basicHeapHandle);
		}
		basicHeapHandle.ptr += inc;
	}
#pragma endregion region 4. アセットの作成とデータ転送
	float angle = 0.0f;
#pragma region 5. メインループ
	bool quit = false;
	while (!quit) {
		if (app.ProcessMessage(quit)) {
			continue;
		}
		else
		{
			angle += 0.03f;
			worldMat = XMMatrixRotationY(angle);
			mapMatrix->world = worldMat;
			mapMatrix->view = viewMat;
			mapMatrix->proj = projMat;
			// ========= 描画前処理 =========
			dx12.BeginDraw();

			// ========= 実際の描画 =========
			renderer.Draw(dx12, vbView, ibView, basicDescHeap.Get(), indicesNum, vertNum, materials);

			// ========= 描画後処理とGPU同期 =========
			dx12.EndDraw();
		}
	}
#pragma endregion 5. メインループ
	return 0;
}