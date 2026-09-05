#include "Application.h"
#include "RenderGraph/Dx12CommandContext.h"
#include "RenderGraph/Dx12ResourceAllocator.h"
#include "RenderGraph/Frontend/RenderGraph.h"
#include "RenderGraph/Frontend/TexturePool.h"
#include "Dx12Wrapper.h"
#include "PMDRenderer.h"
#include "PMDActor.h"
#include "GregoryRenderer.h"
#include "GregoryActor.h"
#include "PeraRenderer.h"
#include "Scene.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

#include <assert.h>
#include <tchar.h>
#include <vector>

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

// パスとリソースの宣言。ここには GPU コマンドを一切積まない。
// ハンドルはフレーム限りの値（graph.Clear() で無効になる）なので、毎フレームここで作り直す。
void Application::BuildGraph(rg::RenderGraph& graph, uint32_t backbufferId)
{
	using rg::LoadOp;
	using rg::State;
	using rg::TextureHandle;

	const rg::TextureDesc colorDesc{ window_width, window_height,
		rg::Format::RGBA8_UNorm, { 0.5f, 0.5f, 0.5f, 1.0f }, 1.0f };
	const rg::TextureDesc depthDesc{ window_width, window_height,
		rg::Format::D32_Float, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f };
	// クリア値は desc が持ち、LoadOp::Clear の宣言だけでバックエンドがクリアする
	const rg::TextureDesc backbufferDesc{ window_width, window_height,
		rg::Format::RGBA8_UNorm, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f };

	// オフスクリーンと深度は TexturePool が実体を持つ。毎フレーム宣言し直すが、
	// 同じ名前と desc なら同じ物理リソースが返ってくるので確保は初回だけ。
	// 外に実体があるのはバックバッファだけで、これは Import する。
	// requiredFinalState を持つリソースがカリングの根になるので、backbuffer にだけ指定する。
	TextureHandle pera1 = graph.Create("pera1", colorDesc);
	TextureHandle pera2 = graph.Create("pera2", colorDesc);
	TextureHandle depth = graph.Create("depth", depthDesc);
	TextureHandle bb = graph.Import("backbuffer", backbufferDesc, backbufferId,
		State::Present, State::Present);

	// --- 1 枚目のオフスクリーンに 3D を描く ---
	struct ScenePass { TextureHandle color, depth; };
	graph.AddPass<ScenePass>("3D",
		[&](rg::RenderGraph::Builder& b, ScenePass& d) {
			d.color = pera1 = b.SetRenderAttachment(pera1, 0, LoadOp::Clear);
			d.depth = depth = b.SetDepthAttachment(depth, LoadOp::Clear);
		},
		[this](const ScenePass&, rg::CommandContext&) {
			_pmdRenderer->Draw(*_scene);
			_gregoryRenderer->Draw(*_scene);
		});

	// --- 横ぼかし：1 枚目を読んで 2 枚目へ ---
	struct BlurPass { TextureHandle src; };
	graph.AddPass<BlurPass>("BlurH",
		[&](rg::RenderGraph::Builder& b, BlurPass& d) {
			d.src = b.SampledRead(pera1);
			pera2 = b.SetRenderAttachment(pera2, 0, LoadOp::Clear);
		},
		[this](const BlurPass& d, rg::CommandContext& ctx) {
			// 読む先はパスの宣言（SampledRead）で決まっている。
			// ハンドル → physicalId → SRV とたどるだけで、添字は出てこない。
			const auto& allocator = static_cast<Dx12CommandContext&>(ctx).Allocator();
			_peraRenderer->DrawHorizontal(allocator.SrvHeap(),
				allocator.SrvOf(ctx.PhysicalOf(d.src)));
		});

	// --- 縦ぼかし：2 枚目を読んでバックバッファへ ---
	graph.AddPass<BlurPass>("BlurV",
		[&](rg::RenderGraph::Builder& b, BlurPass& d) {
			d.src = b.SampledRead(pera2);
			bb = b.SetRenderAttachment(bb, 0, LoadOp::Clear);  // bb@v0 -> bb@v1
		},
		[this](const BlurPass& d, rg::CommandContext& ctx) {
			const auto& allocator = static_cast<Dx12CommandContext&>(ctx).Allocator();
			_peraRenderer->DrawVertical(allocator.SrvHeap(),
				allocator.SrvOf(ctx.PhysicalOf(d.src)));
		});

	// --- ImGui：バックバッファに上書きする。bb@v1 -> bb@v2 で BlurV の後ろに並ぶ ---
	struct ImGuiPass { TextureHandle target; };
	graph.AddPass<ImGuiPass>("ImGui",
		[&](rg::RenderGraph::Builder& b, ImGuiPass& d) {
			d.target = bb = b.SetRenderAttachment(bb, 0, LoadOp::Load);
		},
		[this](const ImGuiPass&, rg::CommandContext& ctx) {
			auto* cmdList = static_cast<Dx12CommandContext&>(ctx).List();

			ID3D12DescriptorHeap* imguiHeaps[] = { _dx12->GetHeapForImgui().Get() };
			cmdList->SetDescriptorHeaps(1, imguiHeaps);
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
		});
}

void Application::Run()
{
	// 物理リソースの実体を知っているのはこのアロケータだけ。
	// RenderGraph も TexturePool も physicalId しか持ち回らない。
	Dx12ResourceAllocator allocator(_dx12->Device());
	rg::TexturePool pool(allocator);
	rg::RenderGraph graph;

	// スワップチェーンのバッファは実体が枚数分あるので、全部登録して id を控えておく。
	// 毎フレーム RegisterExternal すると id が増え続けてしまう。
	std::vector<uint32_t> backbufferIds;
	for (UINT i = 0; i < _dx12->BackBufferCount(); ++i) {
		backbufferIds.push_back(allocator.RegisterExternalRenderTarget(
			_dx12->GetBackBuffer(i), _dx12->GetBackBufferRTV(i)));
	}

	// メインループ
	while (ProcessMessages()) {
		// --- ImGuiフレーム & UI構築 ---
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGui::SetWindowSize(ImVec2(400, 500), ImGuiCond_::ImGuiCond_FirstUseEver);
		ImGui::Begin("Rendering Test Menu");
		_scene->DrawDebugGui();	// ここでカメラをいじれる
		ImGui::End();
		ImGui::Render();

		// --- 論理更新 ---
		_scene->Update();
		if (_pmdActor) _pmdActor->Update();
		_gregoryActor->Update();

		// --- 描画 ---
		// バックバッファはフレームごとに実体が変わるので、その枚の id で Import する
		graph.Clear();
		BuildGraph(graph, backbufferIds[_dx12->CurrentBackBufferIndex()]);

		pool.BeginFrame();
		// 予算を設定していないので今は失敗しない。超過時の扱いは段階 4 で。
		const bool compiled = graph.Compile(pool);
		assert(compiled && "RenderGraph::Compile failed");
		(void)compiled;

		Dx12CommandContext ctx(_dx12->CommandList(), allocator);
		graph.Execute(ctx);

		_dx12->EndDraw();

		// EndDraw() が WaitForGPU() で完全同期しているので、その場で回収してよい。
		pool.EndFrame(_dx12->FenceVal());
		pool.Reclaim(_dx12->FenceVal());
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