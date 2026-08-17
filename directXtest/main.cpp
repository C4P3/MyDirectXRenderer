
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
	PMDActor _pmdActor(dx12);
	if (!_pmdActor.Load("Model/初音ミク.pmd")) return -1;
#pragma endregion 3. パイプラインの構築

#pragma region 4. アセットの作成とデータ転送

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
	
#pragma endregion region 4. アセットの作成とデータ転送
#pragma region 5. メインループ
	bool quit = false;
	while (!quit) {
		if (app.ProcessMessage(quit)) {
			continue;
		}
		else
		{
			// ========= 描画前処理 =========
			dx12.BeginDraw();
			_pmdActor.Update(renderer, viewMat, projMat);
			// ========= 描画後処理とGPU同期 =========
			dx12.EndDraw();
		}
	}
#pragma endregion 5. メインループ
	return 0;
}