#include "Application.h"
#include "Dx12Wrapper.h"
#include "PMDRenderer.h"
#include "PMDActor.h"
#include "GregoryRenderer.h"
#include "GregoryActor.h"
#include "Scene.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

#include <tchar.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

const unsigned int window_width = 1280;
const unsigned int window_height = 720;


static LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

	if (LRESULT result = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return result;
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

	// dx12
	_dx12.reset(new Dx12Wrapper());
	if (!_dx12->Init(GetWindowHandle(), window_width, window_height)) {
		return false;
	}

	// imgui
	if (ImGui::CreateContext() == nullptr) {
		assert(0);
		return false;
	}

	bool blnResult = ImGui_ImplWin32_Init(_hwnd);
	if (!blnResult) {
		assert(0);
		return false;
	}
	ImGui_ImplDX12_InitInfo imgui_init_info = {};
	imgui_init_info.Device = _dx12->Device();
	imgui_init_info.NumFramesInFlight = 3;
	imgui_init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	imgui_init_info.SrvDescriptorHeap = _dx12->GetHeapForImgui().Get();
	imgui_init_info.LegacySingleSrvCpuDescriptor = _dx12->GetHeapForImgui().Get()->GetCPUDescriptorHandleForHeapStart();
	imgui_init_info.LegacySingleSrvGpuDescriptor = _dx12->GetHeapForImgui().Get()->GetGPUDescriptorHandleForHeapStart();
	imgui_init_info.CommandQueue = _dx12->CommandQueue();
	blnResult = ImGui_ImplDX12_Init(&imgui_init_info);

	// scene
	_scene.reset(new Scene(*_dx12));
	if (!_scene->Init(window_width, window_height)) return false;

	// PMD
	_pmdRenderer.reset(new PMDRenderer(*_dx12));
	if (!_pmdRenderer->Init()) return false; // パイプライン構築
	_pmdActor.reset(new PMDActor(*_dx12));
	if (_pmdActor->Load("Model/初音ミク.pmd")) {
		_pmdRenderer->AddActor(_pmdActor.get());
	}
	else {
		_pmdActor.reset();   // Update() の呼び出し側でも null チェック
	}

	// gregory
	_gregoryRenderer.reset(new GregoryRenderer(*_dx12));
	if (!_gregoryRenderer->Init()) return false;
	_gregoryActor.reset(new GregoryActor(*_dx12));
	if (!_gregoryActor->BuildMesh(16)) return false;
	_gregoryRenderer->AddActor(_gregoryActor.get());

	return true;
}

// 溜まっているメッセージを全件処理する関数
// 戻り値: true = 処理継続 / false = WM_QUIT を検知したため終了
bool ProcessMessages() {
	MSG msg = {};
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			return false; // 即座に終了を知らせる
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return true; // メッセージを全部捌ききったので描画へ進む
}

void Application::Run()
{
	// メインループ
	while (ProcessMessages()) {
		// 本当は論理フレームループ
		_scene->Update();
		if (_pmdActor) _pmdActor->Update();
		_gregoryActor->Update();

		// 本当は描画フレームループ
		// 描画前処理
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		_dx12->BeginDraw();

		// 描画処理
		_pmdRenderer->Draw(*_scene);
		_gregoryRenderer->Draw(*_scene); // ← パイプラインを切り替えて2回目の描画

		ImGui::Begin("Rendering Test Menu");
		ImGui::SetWindowSize(ImVec2(400, 500), ImGuiCond_::ImGuiCond_FirstUseEver);
		ImGui::End();
		ImGui::Render();
		_dx12->CommandList()->SetDescriptorHeaps(
			1, _dx12->GetHeapForImgui().GetAddressOf()
		);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), _dx12->CommandList());
		// 描画後処理とGPU同期
		_dx12->EndDraw();
	}
}
void Application::Terminate()
{
	UnregisterClass(_windowClass.lpszClassName, _windowClass.hInstance);
}

Application& Application::Instance()
{
	static Application instance;
	return instance;
}