
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

#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif

#pragma region 1. ウィンドウの生成
	auto& app = Application::Instance();
	if (!app.Init()) {
		return -1;
	}
#pragma region 5. メインループ
	app.Run();
	app.Terminate();
#pragma endregion 5. メインループ
	return 0;
}