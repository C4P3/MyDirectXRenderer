#include "Application.h"
#include "Dx12Wrapper.h"
#include "PMDRenderer.h"
#include "PMDActor.h"

// ウィンドウ定数
const unsigned int window_width = 1280;
const unsigned int window_height = 720;

LRESULT WindowProcedure(
	HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam
) 
{
	if (msg == WM_DESTROY)	// ウィンドウが破棄されたら呼ばれる
	{
		PostQuitMessage(0);	// OSに対してアプリケーションの終了を伝える
	}
	return DefWindowProc(hwnd, msg, wparam, lparam); // 規定の処理を行う
}

void CreateGameWindow(HWND& hwnd, WNDCLASSEX& windowClass)
{
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.lpfnWndProc = (WNDPROC)WindowProcedure;
	windowClass.lpszClassName = _T("DX12Sample");
	windowClass.hInstance = GetModuleHandle(nullptr);

	RegisterClassEx(&windowClass);

	RECT wrc = { 0, 0, window_width, window_height };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	hwnd = CreateWindow(
		windowClass.lpszClassName,
		_T("DX12 テスト"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		windowClass.hInstance,
		nullptr
	);
}

SIZE Application::GetWindowSize()
{
	SIZE ret;
	ret.cx = window_width;
	ret.cy = window_height;
	return ret;
}

bool Application::Init()
{
	auto result = CoInitializeEx(0, COINIT_MULTITHREADED);
	CreateGameWindow(_hwnd, _windowClass);

	// DirectX12 ラッパー生成＆初期化
	_dx12.reset(new Dx12Wrapper(_hwnd));

	_pmdRenderer.reset(new PMDRenderer(*_dx12));
	_pmdActor.reset(new PMDActor("Model/初音ミク.pmd", *_pmdRenderer));

	return true;
}

void Application::Run()
{
	ShowWindow(_hwnd, SW_SHOW); // ウィンドウ表示
	float angle = 0.0f;
	MSG msg = {};
	unsigned int frame = 0;

	while (true)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// アプリケーション終了時
		if (msg.message == WM_QUIT)
		{
			break;
		}

		// 全体の描画準備
		_dx12->BeginDraw();

		// PMD 用の描画パイプラインに合わせる
		_dx12->CommandList()->SetPipelineState(_pmdRenderer->GetPipelineState());

		// ルートシグネチャも PMD 用に合わせる
		_dx12->CommandList()->SetGraphicsRootSignature(_pmdRenderer->GetRootSignature());

		_dx12->CommandList()->IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

		_dx12->SetScene();

		_pmdActor->Update();
		_pmdActor->Draw();

		_dx12->EndDraw();

		// フリップ
		_dx12->Swapchain()->Present(1, 0);
	}
}

void Application::Terminate()
{
	// クラスの登録解除
	UnregisterClass(_windowClass.lpszClassName, _windowClass.hInstance);
}

Application& Application::Instance()
{
	static Application instance;
	return instance;
}