
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
#include "d3dx12.h"
#include "Application.h"
#include "Dx12Wrapper.h"

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

class BasicRenderer
{
private:
	ComPtr<ID3D12RootSignature> _rootSignature;
	ComPtr<ID3D12PipelineState> _pipelineState;

public:
	// 初期化：シェーダーコンパイル、ルートシグネチャ、PSOの作成を行う
	bool Init(Dx12Wrapper& dx12)
	{
		// dx12.Device() を使ってルートシグネチャやPSOを作成し、
		// メンバ変数の _rootSignature と _pipelineState に格納します。

		HRESULT result;
		
		// ・シェーダーのコンパイル
		ComPtr<ID3DBlob> _vsBlob = nullptr;
		ComPtr<ID3DBlob> _psBlob = nullptr;

		// コンパイルとエラー出力を一括で扱うローカル関数
		auto compileShader = [](const wchar_t* fileName, const char* entryPoint, const char* target, ComPtr<ID3DBlob>& outBlob) -> bool {
			ComPtr<ID3DBlob> errorBlob = nullptr;

			HRESULT hr = D3DCompileFromFile(
				fileName,
				nullptr,
				D3D_COMPILE_STANDARD_FILE_INCLUDE,
				entryPoint, target,
				D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
				0,
				&outBlob, &errorBlob
			);

			if (FAILED(hr)) {
				if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
					::OutputDebugStringA("ファイルが見当たりません\n");
				}
				else if (errorBlob) {
					std::string errstr(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
					errstr += "\n";
					::OutputDebugStringA(errstr.c_str());
				}
				return false;
			}
			return true;
			};

		if (!compileShader(L"BasicVertexShader.hlsl", "BasicVS", "vs_5_0", _vsBlob)) return -1;
		if (!compileShader(L"BasicPixelShader.hlsl", "BasicPS", "ps_5_0", _psBlob)) return -1;

		// ルートシグネチャの作成
		// ディスクリプタレンジ
		D3D12_DESCRIPTOR_RANGE descTblRange[2] = {}; // テクスチャと定数の2つ
		// テクスチャ用レジスター0番
		descTblRange[0].NumDescriptors = 1; // テクスチャ１つ
		descTblRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // 種別はテクスチャ
		descTblRange[0].BaseShaderRegister = 0; // 0番スロットから
		descTblRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		// 定数用レジスター1番
		descTblRange[1].NumDescriptors = 1; // 定数1つ
		descTblRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV; // 種別は定数
		descTblRange[1].BaseShaderRegister = 0; // 0番スロットから
		descTblRange[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER rootparam = {};
		rootparam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootparam.DescriptorTable.pDescriptorRanges = descTblRange;
		rootparam.DescriptorTable.NumDescriptorRanges = 2;
		rootparam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		//D3D12_ROOT_PARAMETER rootparam[2] = {};
		//rootparam[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		//rootparam[0].DescriptorTable.pDescriptorRanges = &descTblRange[0];
		//rootparam[0].DescriptorTable.NumDescriptorRanges = 1;
		//rootparam[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;//ピクセルシェーダーから見える
		//rootparam[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		//rootparam[1].DescriptorTable.pDescriptorRanges = &descTblRange[1];
		//rootparam[1].DescriptorTable.NumDescriptorRanges = 1;
		//rootparam[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;//頂点シェーダーから見える

		// ルートシグネチャ
		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};

		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		rootSignatureDesc.pParameters = &rootparam;
		rootSignatureDesc.NumParameters = 1;

		//サンプラーの設定
		D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
		samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		samplerDesc.MinLOD = 0.0f;
		samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

		rootSignatureDesc.pStaticSamplers = &samplerDesc;
		rootSignatureDesc.NumStaticSamplers = 1;

		ComPtr<ID3DBlob> rootSigBlob = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		result = D3D12SerializeRootSignature(
			&rootSignatureDesc,
			D3D_ROOT_SIGNATURE_VERSION_1_0,
			&rootSigBlob,
			&errorBlob
		);
		if (FAILED(result)) {
			if (errorBlob) {
				OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
			}
			return -1;
		}
		result = dx12.Device()->CreateRootSignature(
			0,
			rootSigBlob->GetBufferPointer(),
			rootSigBlob->GetBufferSize(),
			IID_PPV_ARGS(&_rootSignature)
		);
		if (FAILED(result)) return -1;
		rootSigBlob.Reset();

		// ・パイプラインステートオブジェクト(PSO)の作成
		// シェーダーのセット
		D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline = {};
		gpipeline.pRootSignature = _rootSignature.Get();

		gpipeline.VS.pShaderBytecode = _vsBlob->GetBufferPointer();
		gpipeline.VS.BytecodeLength = _vsBlob->GetBufferSize();
		gpipeline.PS.pShaderBytecode = _psBlob->GetBufferPointer();
		gpipeline.PS.BytecodeLength = _psBlob->GetBufferSize();

		// サンプルマスクとラスタライザーステート
		// デフォルトのサンプルマスクを表す定数 (0xffffffff)
		gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

		// まだアンチエイリアスを使わないため false
		gpipeline.RasterizerState.MultisampleEnable = false;

		gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // カリングしない
		gpipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; // 中身塗りつぶし
		gpipeline.RasterizerState.DepthClipEnable = true; // 深度方向のクリッピングは有効に

		gpipeline.BlendState.AlphaToCoverageEnable = false;
		gpipeline.BlendState.IndependentBlendEnable = false;

		D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {};
		renderTargetBlendDesc.BlendEnable = false;
		renderTargetBlendDesc.LogicOpEnable = false;
		renderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		gpipeline.BlendState.RenderTarget[0] = renderTargetBlendDesc;

		// 頂点レイアウト
		D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
			{// 12バイト
				"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
				D3D12_APPEND_ALIGNED_ELEMENT,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			},
			{// 12バイト
				"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,
				0, D3D12_APPEND_ALIGNED_ELEMENT,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			},
			{// 8バイト
				"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
				0, D3D12_APPEND_ALIGNED_ELEMENT,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			},
			{// 4バイト
				"BONE_NO", 0, DXGI_FORMAT_R16G16_UINT,
				0, D3D12_APPEND_ALIGNED_ELEMENT,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			},
			{// 1バイト
				"WEIGHT", 0, DXGI_FORMAT_R8_UINT,
				0, D3D12_APPEND_ALIGNED_ELEMENT,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			},
			{// 1バイト
				"EDGE_FLG", 0, DXGI_FORMAT_R8_UINT,
				0, D3D12_APPEND_ALIGNED_ELEMENT,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			}
		};

		// ・ビューポートとシザー矩形の設定
		gpipeline.InputLayout.pInputElementDescs = inputLayout; // レイアウト先頭アドレス
		gpipeline.InputLayout.NumElements = _countof(inputLayout); // レイアウト配列の要素数

		gpipeline.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;

		//三角形で構成
		gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		gpipeline.NumRenderTargets = 1;
		gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

		gpipeline.SampleDesc.Count = 1;
		gpipeline.SampleDesc.Quality = 0;

		// 深度バッファー
		gpipeline.DepthStencilState.DepthEnable = true;
		gpipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; // 書き込む
		gpipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS; // 小さいほうを採用

		gpipeline.DepthStencilState.StencilEnable = false;

		gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;


		result = dx12.Device()->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(&_pipelineState));
		if (FAILED(result))return -1;

		return true;
	}

	// 描画コマンドの積み込み
	void Draw(Dx12Wrapper& dx12,
		const D3D12_VERTEX_BUFFER_VIEW& vbView,
		const D3D12_INDEX_BUFFER_VIEW& ibView,
		ID3D12DescriptorHeap* descHeap,
		int indicesNum, int vertNum)
	{
		auto cmdList = dx12.CommandList();

		// パイプラインの設定
		cmdList->SetPipelineState(_pipelineState.Get());
		cmdList->SetGraphicsRootSignature(_rootSignature.Get());

		// テクスチャ（ヒープ）のセット
		ID3D12DescriptorHeap* ppHeaps[] = { descHeap };
		cmdList->SetDescriptorHeaps(1, ppHeaps);

		cmdList->SetGraphicsRootDescriptorTable(0, descHeap->GetGPUDescriptorHandleForHeapStart());
		//auto handle = descHeap->GetGPUDescriptorHandleForHeapStart();
		//cmdList->SetGraphicsRootDescriptorTable(0, handle); // [0] SRV
		//handle.ptr += dx12.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		//cmdList->SetGraphicsRootDescriptorTable(1, handle); // [1] CBV

		// ジオメトリのセットと描画
		//cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
		cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmdList->IASetVertexBuffers(0, 1, &vbView);
		cmdList->IASetIndexBuffer(&ibView);

		//cmdList->DrawInstanced(vertNum, 1, 0, 0);
		cmdList->DrawIndexedInstanced(indicesNum, 1, 0, 0, 0);
	}
};

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
	BasicRenderer renderer;
	renderer.Init(dx12); // パイプライン構築
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

	FILE *fp; 
	fopen_s(&fp, "Model/初音ミク.pmd", "rb");
	if (fp == nullptr) return -1;
	fread(signature, 3, 1, fp);
	fread(&pmdheader, sizeof(pmdheader), 1, fp);

	fread(&vertNum, sizeof(vertNum), 1, fp); // 頂点数はヘッダーデータ直後
	rawVertices.resize(vertNum * vert_raw_size); // バッファーの確保
	fread(rawVertices.data(), rawVertices.size(), 1, fp);

	fread(&indicesNum, sizeof(indicesNum), 1, fp);
	indices.resize(indicesNum);
	fread(indices.data(), indices.size() * sizeof(unsigned short), 1, fp);
	fclose(fp);

	// 入力レイアウトに R32G32B32_FLOAT（4バイト単位の型）が入っているので、
	// ストライドも 4 の倍数でなければいけない
	vertices.resize(vertNum);
	for (unsigned i = 0; i < vertNum; ++i) {
		// 必要な38バイト分だけコピー（残りのpaddingは0初期化される）
		memcpy(&vertices[i], &rawVertices[i], vert_raw_size);
	}

	
	
	std::cout << signature << '\n';
	std::cout << pmdheader.version << '\n';
	std::cout << pmdheader.model_name << '\n';
	std::cout << pmdheader.comment << '\n';
	std::cout << vertNum << '\n';
	std::cout << indicesNum << '\n';



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
	XMFLOAT3 eye(0, 10, -15);
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
	size_t cbSize = (sizeof(matrix) + 255) & ~255; // 256バイトアライメント

	// 定数バッファ
	//ComPtr<ID3D12Resource> constBuff = dx12.CreateBuffer(cbSize, &matrix, sizeof(matrix));
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
	XMMATRIX* mappedPtr = nullptr;
	if (&matrix != nullptr) {
		// CPUから読み込まないことを明確にするため Range(0, 0) を指定
		CD3DX12_RANGE readRange(0, 0);
		hr = constBuff->Map(0, &readRange, (void**)&mappedPtr);

		if (SUCCEEDED(hr)) {
			// dataSize が指定されていなければ sizeInBytes を使用
			size_t copySize = (sizeof(matrix) > 0) ? sizeof(matrix) : cbSize;
			std::memcpy(mappedPtr, &matrix, copySize);

			// 書き込んだ範囲を指定して Unmap (nullptr でも可)
			//CD3DX12_RANGE writeRange(0, copySize);
			//constBuff->Unmap(0, &writeRange);
		}
	}

	// テクスチャのロード（SRVはまだ作らない）
	ComPtr<ID3D12Resource> texbuff = dx12.CreateTextureFromFile(L"img/tsukimi_jugoya.png");

	// 2. 定数バッファビューをディスクリプタヒープに追加する
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descHeapDesc.NodeMask = 0;
	descHeapDesc.NumDescriptors = 2; // SRV 1 つとCBV 1つ
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	ComPtr<ID3D12DescriptorHeap> basicDescHeap = nullptr;
	result = dx12.Device()->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&basicDescHeap));
	if (FAILED(result)) return -1;

	// ディスクリプタの先頭ハンドルを取得しておく
	auto basicHeapHandle = basicDescHeap->GetCPUDescriptorHandleForHeapStart();

	// [0] SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texbuff->GetDesc().Format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	dx12.Device()->CreateShaderResourceView(texbuff.Get(), &srvDesc, basicHeapHandle);

	// [1] CBV
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = constBuff->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = static_cast<UINT>(constBuff->GetDesc().Width);
	// 次の場所に移動
	basicHeapHandle.ptr += dx12.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	dx12.Device()->CreateConstantBufferView(&cbvDesc, basicHeapHandle);

	// 4. シェーダから利用する
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
			angle += 0.1f;
			worldMat = XMMatrixRotationY(angle);
			*mappedPtr = worldMat * viewMat * projMat;
			// ========= 描画前処理 =========
			dx12.BeginDraw();

			// ========= 実際の描画 =========
			renderer.Draw(dx12, vbView, ibView, basicDescHeap.Get(), indicesNum, vertNum);

			// ========= 描画後処理とGPU同期 =========
			dx12.EndDraw();
		}
	}
#pragma endregion 5. メインループ
	return 0;
}