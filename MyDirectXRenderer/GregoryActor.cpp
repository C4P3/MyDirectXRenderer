#define MATERIAL_MULTIPLIER 5

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <wrl/client.h> // ComPtr用
#include <string>
#include <DirectXMath.h>
#include <DirectXTex.h>
#include <algorithm>
#include <map>

#include "d3dx12.h"
#include "Application.h"
#include "Dx12Wrapper.h"
#include "GregoryActor.h"
#include "PMDActor.h"
#include "core/lattice.h"
#include "core/patch_mesh.h"
#include "core/gregory.h"
#include "core/g1.h"


#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "DirectXTex.lib")
#pragma comment(lib, "dxguid.lib")

using namespace std;
using namespace DirectX;
using Microsoft::WRL::ComPtr;


// Load() 相当。段階 B では格子を編集しないので初回1回だけで良い
bool GregoryActor::BuildMesh(int segments)
{
	_lattice = greg::makeCube();
	_patchMesh = greg::roundLattice(_lattice);

	std::vector<GregoryVertex> verts;
	for (auto& patch : _patchMesh.patches) {
		for (int j = 0; j <= segments; ++j) {
			for (int i = 0; i <= segments; ++i) {
				float u = float(i) / segments;
				float v = float(j) / segments;
				auto s = greg::sample(patch, u, v);
				verts.push_back({ {s.position.x, s.position.y, s.position.z},
								  {s.normal.x,   s.normal.y,   s.normal.z  } });
			}
		}
	}

	const int vertsPerPatch = (segments + 1) * (segments + 1);
	std::vector<uint32_t> idx;
	for (int p = 0; p < _patchMesh.patches.size(); ++p) {
		uint32_t base = p * vertsPerPatch;
		for (int j = 0; j < segments; ++j) {
			for (int i = 0; i < segments; ++i) {
				uint32_t a = base + j * (segments + 1) + i;
				uint32_t b = a + 1;                  // +Δu
				uint32_t c = a + (segments + 1);     // +Δv
				uint32_t d = c + 1;
				// 解析法線 du×dv の側から見て CCW になる順序
				idx.insert(idx.end(), { a, b, c, b, d, c });
			}
		}
	}

	_vertBuff = _dx12.CreateBuffer(verts.size() * sizeof(GregoryVertex), verts.data());
	_idxBuff = _dx12.CreateBuffer(idx.size() * sizeof(uint32_t), idx.data());

	// 頂点バッファービュー
	_vbView.BufferLocation = _vertBuff->GetGPUVirtualAddress(); // バッファーの仮想アドレス
	_vbView.SizeInBytes = verts.size() * sizeof(GregoryVertex);	// 全バイト数
	_vbView.StrideInBytes = sizeof(GregoryVertex);	// 一頂点辺りのバイト数

	// インデックスバッファービューを作成
	_ibView.BufferLocation = _idxBuff->GetGPUVirtualAddress();
	_ibView.Format = DXGI_FORMAT_R32_UINT;
	_ibView.SizeInBytes = static_cast<UINT>(idx.size() * sizeof(uint32_t));
	_indexCount = static_cast<unsigned int>(idx.size());


	XMMATRIX matrix = XMMatrixIdentity();

	// 1. 定数バッファの作成して中身をマップで書き換える（バッファサイズ: 256バイト、コピー元サイズ: sizeof(matrix) = 64バイト）
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
		IID_PPV_ARGS(&_transformBuff)
	);
	if (FAILED(hr)) return false;

	// CPUから読み込まないことを明確にするため Range(0, 0) を指定
	CD3DX12_RANGE readRange(0, 0);
	hr = _transformBuff->Map(0, &readRange, (void**)&_mappedTransform);

	if (FAILED(hr)) return false;
	return true;
};
void GregoryActor::Update() {
	angle += 0.01f;
	_worldMatrix = XMMatrixScaling(5, 5, 5)
		* XMMatrixRotationX(angle)
		* XMMatrixTranslation(10, 10, 0);
	_mappedTransform->world = _worldMatrix;
};
void GregoryActor::Draw() {
	// ========= 実際の描画 =========
	auto cmdList = _dx12.CommandList();

	// ワールド行列（b2）をルートCBVで直接渡す
	cmdList->SetGraphicsRootConstantBufferView(1, _transformBuff->GetGPUVirtualAddress());
	// ジオメトリのセットと描画
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->IASetVertexBuffers(0, 1, &_vbView);
	cmdList->IASetIndexBuffer(&_ibView);
	cmdList->DrawIndexedInstanced(_indexCount, 1, 0, 0, 0);
};