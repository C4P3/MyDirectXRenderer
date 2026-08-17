#include "Application.h"
#include "Dx12Wrapper.h"
#include "PMDRenderer.h"
#include "PMDActor.h"
#include <tchar.h>
using namespace DirectX;

const unsigned int window_width = 1280;
const unsigned int window_height = 720;
XMMATRIX viewMat;
XMMATRIX projMat;

static LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

// コンストラクタの実装
Application::~Application() = default;

// ウィンドウの生成
bool Application::Init() {
	_windowClass.cbSize = sizeof(WNDCLASSEX);
	_windowClass.lpfnWndProc = (WNDPROC)WindowProcedure;
	_windowClass.lpszClassName = _T("DX12Sample");
	_windowClass.hInstance = GetModuleHandle(nullptr);

	RegisterClassEx(&_windowClass);

	RECT wrc = { 0, 0, window_width, window_height };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	_hwnd = CreateWindow(
		_windowClass.lpszClassName,
		_T("DX12 テスト"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		_windowClass.hInstance,
		nullptr
	);

	if (!_hwnd) return false;

	ShowWindow(_hwnd, SW_SHOW);

	#pragma region 3. パイプラインの構築
		_dx12.reset(new Dx12Wrapper());
		if (!_dx12->Init(GetWindowHandle(), window_width, window_height)) {
			return -1;
		}
		_pmdRenderer.reset(new PMDRenderer(*_dx12));
		if (!_pmdRenderer->Init()) return -1; // パイプライン構築
		_pmdActor.reset(new PMDActor(*_dx12));
		if (!_pmdActor->Load("Model/初音ミク.pmd")) return -1;
	#pragma endregion 3. パイプラインの構築

	#pragma region 4. アセットの作成とデータ転送

		// ビュー行列
		XMFLOAT3 eye(0, 15, -15);
		XMFLOAT3 target(0, 10, 0);
		XMFLOAT3 up(0, 1, 0);
		viewMat = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&up));

		//プロジェクション行列
		projMat = XMMatrixPerspectiveFovLH(
			XM_PIDIV2,
			static_cast<float>(window_width) / static_cast<float>(window_height),
			1.0f, // 近いクリップ面距離
			100.0f // 遠いクリップ面距離
		);

	#pragma endregion region 4. アセットの作成とデータ転送
	return true;
}

// メインループ前半
// 戻り値: メッセージを処理したか（trueなら描画せず次のループへ、falseなら描画フェーズへ）
bool ProcessMessage(bool& quit) {
	MSG msg = {};
	if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			quit = true;
		}
		else {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		return true; // メッセージがあった（元のコードの if(PeekMessage) 内に相当）
	}
	return false; // メッセージなし（元のコードの else 句に相当、描画タイミング）
}

void Application::Run()
{
	bool quit = false;
	while (!quit) {
		if (ProcessMessage(quit)) {
			continue;
		}
		else
		{
			// ========= 描画前処理 =========
			_dx12->BeginDraw();
			_pmdActor->Update(*_pmdRenderer, viewMat, projMat);
			// ========= 描画後処理とGPU同期 =========
			_dx12->EndDraw();
		}
	}
}
void Application::Terminate()
{
	1 + 1;
}

Application& Application::Instance()
{
	static Application instance;
	return instance;
}