#include "GameApp.h"

#include <chrono>

#include "Scene/SceneManager.h"
#include "Scene/SceneFactory.h"

#include "DirectXCore.h"
#include "SRVManager.h"
#include "GPUParticleManager.h"
#include "Effect/EffectManager.h"
#include "IImGuiEditable.h"
#include "Physics/CollisionSystem.h"
#include "PostEffect.h"
#include "RenderTexture.h"
#include "WindowsApplication.h"
#include "Object3DManager.h"
#include "Camera.h"
// ImGuiManager の各メソッドは Release では中身が空展開されるので、include は常に行う
#include "ImGuiManager.h"

#ifdef _DEBUG
#include "Effect/EffectEditorWindow.h"
#include "Scene.h"
#endif

GameApp* GameApp::instance_ = nullptr;

GameApp::GameApp()  { instance_ = this; }
GameApp::~GameApp() { instance_ = nullptr; }

ISceneRunner* GameApp::GetSceneRunner() {
	// シーン駆動の実体は SceneManager。Framework はこの IF 経由で回す
	return SceneManager::GetInstance();
}

void GameApp::Initialize() {
	//===================================
	// エンティティの生成/破棄フックを最初に配線する（依存性の逆転）。
	// 以降に作られる全 IImGuiEditable がここを通り、エディタと当たり判定に登録される。
	// これを配線しないと Hierarchy / Inspector に何も出ない。
	//===================================
	IImGuiEditable::SetHooks(
		[](IImGuiEditable* e) {
			ImGuiManager::Instance().Register(e);
			CollisionSystem::GetInstance()->Register(e);
		},
		[](IImGuiEditable* e) {
			CollisionSystem::GetInstance()->Unregister(e);
			ImGuiManager::Instance().Unregister(e);
		});

	//===================================
	// シーン工場を SceneManager へ渡す。
	// Framework::Initialize の中で SceneManager::Initialize が呼ばれ、
	// そこで最初のシーンが生成されるので、その前に設定しておく。
	//===================================
	sceneFactory_ = std::make_unique<SceneFactory>();
	SceneManager::GetInstance()->SetSceneFactory(sceneFactory_.get());
	SceneManager::GetInstance()->SetStartScene("Title");

	Framework::Initialize();

	//===================================
	// エフェクト系。
	// Debug ビルドの Effect Editor はこれらが初期化されている前提で動く。
	//===================================
	gpuParticleManager_ = std::make_unique<GPUParticleManager>();
	gpuParticleManager_->Initialize(dxCore_.get(), srvManager_.get());
	gpuParticleManager_->CreateGroup("spark", "Resources/Textures/circle.dds");

	EffectManager::GetInstance()->Initialize(gpuParticleManager_.get());
	EffectManager::GetInstance()->LoadAllDefsInDirectory("Resources/Json/Effects");

	//===================================
	// ポストエフェクト。シーンを一度 RT に描いてからフィルタ合成する。
	// 主目的はエフェクトの画面歪み（ポータルの Warp）。歪み源が無いフレームはパスをスキップする。
	//===================================
	postEffect_ = std::make_unique<PostEffect>();
	// シーン RT を背景色でクリアさせる（何も描かれていない領域がこの色になる）。
	const float sceneClearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
	postEffect_->Initialize(dxCore_.get(), srvManager_.get(),
		WindowsApplication::kClientWidth, WindowsApplication::kClientHeight, sceneClearColor);

#ifdef _DEBUG
	ImGuiManager::Instance().SetGPUParticleManager(gpuParticleManager_.get());

	//===================================
	// エディタ核（エンジン）がアプリの実体へ触るためのフックを配線する。
	// 未配線でも動くが、配線するとカメラ/タイムライン等のパネルが有効になる。
	//===================================
	{
		EditorHostHooks hooks{};
		hooks.getActiveScene = []() -> Scene* {
			return SceneManager::GetInstance()->GetCurrentScene();
		};
		hooks.getActiveSceneName = []() -> const char* {
			return SceneManager::GetInstance()->GetCurrentSceneName().c_str();
		};
		hooks.getFramework = []() -> Framework* { return GameApp::GetInstance(); };
		hooks.getPostEffect = []() -> PostEffect* { return GameApp::GetPostEffect(); };
		// エンティティのグループ分けはしていないので Hierarchy は 1 グループになる。
		ImGuiManager::SetHostHooks(hooks);
	}

	// Scene ビューポートの表示元。シーンはここへ描き、ImGui が SRV として表示する
	viewportRenderTexture_ = std::make_unique<RenderTexture>();
	const float viewportClearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
	viewportRenderTexture_->Initialize(dxCore_.get(), srvManager_.get(),
		WindowsApplication::kClientWidth,
		WindowsApplication::kClientHeight,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		viewportClearColor);
	ImGuiManager::Instance().SetViewportRenderTexture(viewportRenderTexture_.get());
#endif
}

void GameApp::Finalize() {
#ifdef _DEBUG
	ImGuiManager::Instance().SetGPUParticleManager(nullptr);
	ImGuiManager::Instance().SetViewportRenderTexture(nullptr);
	if (viewportRenderTexture_) {
		viewportRenderTexture_->Finalize();
		viewportRenderTexture_.reset();
	}
#endif

	// エフェクト系は GPU を止める前に解放する
	EffectManager::GetInstance()->Finalize();
	if (gpuParticleManager_) {
		gpuParticleManager_->Finalize();
		gpuParticleManager_.reset();
	}
	if (postEffect_) {
		postEffect_->Finalize();
		postEffect_.reset();
	}

	Framework::Finalize();   // 中で SceneManager::Finalize が呼ばれる
	sceneFactory_.reset();
}

void GameApp::RenderSceneWithPostEffect(RenderTexture* output) {
	auto* cmd = dxCore_->GetCommandList();
	auto* sceneManager = SceneManager::GetInstance();

	const uint32_t w = WindowsApplication::kClientWidth;
	const uint32_t h = WindowsApplication::kClientHeight;

	//---------------------------------------------
	// 1. シーンを postEffect_ の RT へ描く。
	//    RT の色クリアは BeginSceneRender 内で行われる（Initialize で渡した背景色）。
	//---------------------------------------------
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCore_->GetDsvHandle();
	postEffect_->BeginSceneRender(cmd, &dsv);
	srvManager_->PreDraw();
	cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	sceneManager->Draw();
	sceneManager->DrawTransition();   // シーンの上に覆いかぶさる（フィルタも掛かる）

	postEffect_->EndSceneRender(cmd);

	//---------------------------------------------
	// 1.5 状態異常アウトライン用 ID パス。
	//     炎/氷のキャラを idMaskRT へシルエット描画 → MaskedOutline フィルタが縁取る。
	//     idMaskRT は R8_UINT で 1=炎 / 2=氷。状態のキャラが1体も居なければフィルタは OFF のまま。
	//---------------------------------------------
	if (postEffect_->maskedOutline) {
		// 点滅用の時刻（steady_clock ベース、dt 非依存）。
		static const auto s_outlineStart = std::chrono::steady_clock::now();
		const float t = std::chrono::duration<float>(
			std::chrono::steady_clock::now() - s_outlineStart).count();
		postEffect_->maskedOutline->SetTime(t);

		bool drewAny = false;
		if (statusOutlineDrawer_) {
			postEffect_->BeginIdPass(cmd);
			// WriteID PSO は深度テストあり(書き込み無し)なので RTV+DSV を明示バインドする。
			auto idRtv = postEffect_->GetIdMaskRT()->GetRTVHandle();
			auto idDsv = dxCore_->GetDsvHandle();
			cmd->OMSetRenderTargets(1, &idRtv, false, &idDsv);
			D3D12_VIEWPORT idVp{ 0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f };
			D3D12_RECT idSc{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
			cmd->RSSetViewports(1, &idVp);
			cmd->RSSetScissorRects(1, &idSc);
			drewAny = statusOutlineDrawer_(cmd);
			postEffect_->EndIdPass(cmd);
		}
		postEffect_->maskedOutline->SetEnabled(drewAny);
		postEffect_->maskedOutline->UpdateConstantBuffer();
	}

	//---------------------------------------------
	// 2. 歪みパス。useDistortion なエフェクトプリミティブが歪みマップを distortionRT へ書き込む。
	//    歪み源が無いフレームはパス全体をスキップ（GPU 節約。合成側フィルタも同じフラグで ON/OFF）。
	//---------------------------------------------
	const bool distortionActive = EffectManager::GetInstance()->HasActiveDistortionSource();
	if (postEffect_->distortion) {
		postEffect_->distortion->SetEnabled(distortionActive);
	}
	if (distortionActive) {
		postEffect_->BeginDistortionPass(cmd);
		// BeginDistortionPass は distortionRT のクリアだけ。歪みプリミティブは深度テスト（書き込みなし）
		// を行う PSO なので、RTV + DSV を明示バインドしないと null DSV で #615 になる。
		auto rtv = postEffect_->GetDistortionRT()->GetRTVHandle();
		auto ddsv = dxCore_->GetDsvHandle();
		cmd->OMSetRenderTargets(1, &rtv, false, &ddsv);
		D3D12_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f };
		D3D12_RECT scissor{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
		cmd->RSSetViewports(1, &vp);
		cmd->RSSetScissorRects(1, &scissor);
		srvManager_->PreDraw();
		EffectManager::GetInstance()->DrawDistortionPass();
		postEffect_->EndDistortionPass(cmd);
	}

	//---------------------------------------------
	// 3. フィルタ合成して output（nullptr ならスワップチェーン）へ出力
	//---------------------------------------------
	if (!output) {
		dxCore_->BeginDraw(); // スワップチェーン出力時は Draw の前に呼ぶ必要がある
		srvManager_->PreDraw();
	}
	if (Camera* cam = object3DManager_->GetDefaultCamera()) {
		postEffect_->SetProjectionMatrix(cam->GetProjectionMatrix()); // Outline 系が射影行列を要る
	}
	postEffect_->Draw(cmd, output);
}

void GameApp::Draw() {
	//   Debug   : シーン(+PostEffect) → Scene ビューポート用 RT、スワップチェーンには ImGui だけ
	//   Release : シーン(+PostEffect) → スワップチェーンへ直接

#ifdef _DEBUG
	const float clearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };

	//---------------------------------------------
	// 1. シーンを PostEffect 経由で Scene ビューポート用 RT へ描く
	//---------------------------------------------
	RenderSceneWithPostEffect(viewportRenderTexture_.get());

	//---------------------------------------------
	// 2. Effect Editor のプレビュー RT
	//    毎フレーム呼ばないと RENDER_TARGET のまま残り、
	//    ImGui::Image が SRV としてバインドした瞬間に GPU ベース検証が落ちる。
	//---------------------------------------------
	if (auto* editor = ImGuiManager::Instance().GetEffectEditorWindow()) {
		editor->Render();
	}

	//---------------------------------------------
	// 3. スワップチェーンには ImGui だけを描く
	//---------------------------------------------
	dxCore_->BeginDraw();
	dxCore_->ClearRenderTarget(clearColor);
	srvManager_->PreDraw();

	ImGuiManager::Instance().EndFrame();
#else
	RenderSceneWithPostEffect(nullptr); // 中で dxCore_->BeginDraw する
#endif

	dxCore_->EndDraw();
	dxCore_->TickIntermediateResources();
	dxCore_->TickPendingCallbacks();
}
