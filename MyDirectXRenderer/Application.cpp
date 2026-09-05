#include "Application.h"
#include "RenderGraph/LegacyRenderGraph.h"
#include "Dx12Wrapper.h"
#include "PMDRenderer.h"
#include "PMDActor.h"
#include "GregoryRenderer.h"
#include "GregoryActor.h"
#include "PeraRenderer.h"
#include "RenderPasses.h"
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
	imgui_init_info.NumFramesInFlight = 2;// ワップチェーンのバックバッファの数 CPUがGPUを待たずに何フレーム分先まで走ってよいか
	imgui_init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	imgui_init_info.SrvDescriptorHeap = _dx12->GetHeapForImgui().Get();
	imgui_init_info.LegacySingleSrvCpuDescriptor = _dx12->GetHeapForImgui().Get()->GetCPUDescriptorHandleForHeapStart();
	imgui_init_info.LegacySingleSrvGpuDescriptor = _dx12->GetHeapForImgui().Get()->GetGPUDescriptorHandleForHeapStart();
	imgui_init_info.CommandQueue = _dx12->CommandQueue();
	blnResult = ImGui_ImplDX12_Init(&imgui_init_info);

	// scene
	_scene.reset(new Scene(*_dx12));
	if (!_scene->Init(window_width, window_height)) return false;

	// マルチパスレンダラー
	_peraRenderer.reset(new PeraRenderer(*_dx12));
	if (!_peraRenderer->Init()) return false; // パイプライン構築

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
	// VMD アニメーション
	if (_pmdActor) _pmdActor->VMDMotionLoad("Motion/squat.vmd");


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
	RenderGraph renderGraph;

	// 初期状態＝「毎フレーム開始時の状態」を登録する
	renderGraph.RegisterResource("BackBuffer", _dx12->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
	renderGraph.RegisterResource("DepthBuffer", _dx12->GetDepthBuffer(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
	renderGraph.RegisterResource("PeraResource1", _dx12->GetPeraResource1(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	renderGraph.RegisterResource("PeraResource2", _dx12->GetPeraResource2(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	renderGraph.AddPass(std::make_unique<Pass1_Main3D>(_dx12.get(), _pmdRenderer.get(), _gregoryRenderer.get(), _scene.get()));
	renderGraph.AddPass(std::make_unique<Pass2_HorizontalBlur>(_dx12.get(), _peraRenderer.get()));
	renderGraph.AddPass(std::make_unique<Pass3_VerticalBlurAndUI>(_dx12.get(), _peraRenderer.get()));


	// メインループ
	while (ProcessMessages()) {
		// --- ImGuiフレーム & UI構築 ---
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGui::SetWindowSize(ImVec2(400, 500), ImGuiCond_::ImGuiCond_FirstUseEver);
		ImGui::Begin("Rendering Test Menu");
		_scene->DrawDebugGui();	// ここでカメラをいじる
		ImGui::End();
		ImGui::Render();

		// --- 論理更新 ---
		_scene->Update();
		if (_pmdActor) _pmdActor->Update();
		_gregoryActor->Update();

		//// --- 描画 ---
		// バックバッファはフレームごとに実体が変わるので毎回差し替える
		renderGraph.UpdateResource("BackBuffer", _dx12->GetCurrentBackBuffer());

		renderGraph.Compile();
		renderGraph.Execute(_dx12->CommandList());

		_dx12->EndDraw();
	}
}
void Application::Terminate()
{
	UnregisterClass(_windowClass.lpszClassName, _windowClass.hInstance);
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

Application& Application::Instance()
{
	static Application instance;
	return instance;
}