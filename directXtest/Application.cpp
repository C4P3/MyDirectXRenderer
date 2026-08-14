#include "Application.h"
#include <tchar.h>

static LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

// コンストラクタの実装
Application::Application(int width, int height) : window_width(width), window_height(height) {}

// デストラクタの実装
Application::~Application() {
    if (hwnd) {
        UnregisterClass(w.lpszClassName, w.hInstance);
    }
}

// ウィンドウの生成
bool Application::Init() {
	w.cbSize = sizeof(WNDCLASSEX);
	w.lpfnWndProc = (WNDPROC)WindowProcedure;
	w.lpszClassName = _T("DX12Sample");
	w.hInstance = GetModuleHandle(nullptr);

	RegisterClassEx(&w);

	RECT wrc = { 0, 0, window_width, window_height };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	hwnd = CreateWindow(
		w.lpszClassName,
		_T("DX12 テスト"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		w.hInstance,
		nullptr
	);

	if (!hwnd) return false;

	ShowWindow(hwnd, SW_SHOW);
	return true;
}

// メインループ前半
// 戻り値: メッセージを処理したか（trueなら描画せず次のループへ、falseなら描画フェーズへ）
bool Application::ProcessMessage(bool& quit) {
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