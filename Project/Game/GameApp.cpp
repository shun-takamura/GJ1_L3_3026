#include "GameApp.h"

#include "Scene/SceneManager.h"
#include "Scene/SceneFactory.h"

#include "DirectXCore.h"
#include "SRVManager.h"
#include "GPUParticleManager.h"
#include "Effect/EffectManager.h"
#include "IImGuiEditable.h"
#include "Physics/CollisionSystem.h"
// ImGuiManager の各メソッドは Release では中身が空展開されるので、include は常に行う
#include "ImGuiManager.h"

#ifdef _DEBUG
#include "Effect/EffectEditorWindow.h"
#include "RenderTexture.h"
#include "WindowsApplication.h"
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
		// PostEffect は使わない最小構成なので getPostEffect は未配線のまま。
		// エンティティのグループ分けもしていないので Hierarchy は 1 グループになる。
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

	Framework::Finalize();   // 中で SceneManager::Finalize が呼ばれる
	sceneFactory_.reset();
}

void GameApp::Draw() {
	// PostEffect / ID パス / 歪みパスを使わない最小構成。
	//   Debug   : シーン → Scene ビューポート用 RT、スワップチェーンには ImGui だけ
	//   Release : シーン → スワップチェーンへ直接
	auto* cmd = dxCore_->GetCommandList();
	auto* sceneManager = SceneManager::GetInstance();

	const float clearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };

#ifdef _DEBUG
	//---------------------------------------------
	// 1. シーンを Scene ビューポート用 RT へ描く
	//---------------------------------------------
	{
		D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCore_->GetDsvHandle();
		// BeginRender がクリア・RTV/DSV バインド・ビューポート設定までやる
		viewportRenderTexture_->BeginRender(cmd, &dsv);
		cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		srvManager_->PreDraw();
		sceneManager->Draw();
		sceneManager->DrawTransition();   // シーンの上に覆いかぶさる

		// SRV 状態へ戻す（ImGui::Image が読めるように）
		viewportRenderTexture_->EndRender(cmd);
	}

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
	dxCore_->BeginDraw();
	dxCore_->ClearRenderTarget(clearColor);
	srvManager_->PreDraw();

	sceneManager->Draw();
	sceneManager->DrawTransition();
#endif

	dxCore_->EndDraw();
	dxCore_->TickIntermediateResources();
	dxCore_->TickPendingCallbacks();
}
