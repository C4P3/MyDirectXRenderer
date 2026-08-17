#include "Application.h"
#include "Dx12Wrapper.h"
#include "PMDRenderer.h"
#include "PMDActor.h"
#include "Scene.h"
#include <tchar.h>

using namespace DirectX;

const unsigned int window_width = 1280;
const unsigned int window_height = 720;

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
		if (_pmdActor->Load("Model/初音ミク.pmd")) {
			_pmdRenderer->AddActor(_pmdActor.get());
		}
		else {
			_pmdActor.reset();   // Update() の呼び出し側でも null チェック
		}
		_scene.reset(new Scene(*_dx12));
		if (!_scene->Init(window_width, window_height)) return false;
	#pragma endregion 3. パイプラインの構築
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
			// 本当は論理フレームループ
			_pmdActor->Update();

			/*_mapMatrix->view = view;
			_mapMatrix->proj = proj;*/

			// 本当は描画フレームループ
			// ========= 描画前処理 =========
			_dx12->BeginDraw();
			_pmdRenderer->Draw(*_scene);
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