#include "SceneManager.h"

#include "Scene.h"
#include "AbstractSceneFactory.h"
#include "EngineTime.h"
#include "Transition/TransitionManager.h"

#include <cassert>

// Scene の完全型がここで揃うので、ctor/dtor の実体はこの翻訳単位に置く
SceneManager::SceneManager()  = default;
SceneManager::~SceneManager() = default;

SceneManager* SceneManager::GetInstance() {
	static SceneManager instance;
	return &instance;
}

void SceneManager::Initialize(
	SpriteManager* spriteManager,
	Object3DManager* object3DManager,
	SkyboxManager* skyboxManager,
	DirectXCore* dxCore,
	SRVManager* srvManager,
	InputManager* input,
	SkinningComputeManager* skinningComputeManager)
{
	spriteManager_ = spriteManager;
	object3DManager_ = object3DManager;
	skyboxManager_ = skyboxManager;
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	input_ = input;
	skinningComputeManager_ = skinningComputeManager;

	// トランジション。画面サイズは渡さなければ実クライアントサイズが使われる
	TransitionManager::GetInstance()->Initialize(spriteManager_, dxCore_);

	// 最初のシーンへ
	ApplyScene(startSceneName_);
}

void SceneManager::ApplyScene(const std::string& sceneName) {
	assert(sceneFactory_ && "SetSceneFactory を先に呼ぶこと");

	if (currentScene_) {
		currentScene_->Finalize();
		currentScene_.reset();
	}

	currentScene_ = sceneFactory_->CreateScene(sceneName);
	if (!currentScene_) {
		// 名前の綴り間違いなど。SceneFactory::CreateScene を確認する
		assert(false && "未知のシーン名");
		return;
	}
	currentSceneName_ = sceneName;

	// Scene 基底が要求するマネージャを注入する
	currentScene_->SetSpriteManager(spriteManager_);
	currentScene_->SetObject3DManager(object3DManager_);
	currentScene_->SetSkyboxManager(skyboxManager_);
	currentScene_->SetDirectXCore(dxCore_);
	currentScene_->SetSRVManager(srvManager_);
	currentScene_->SetInputManager(input_);
	currentScene_->SetSkinningComputeManager(skinningComputeManager_);

	currentScene_->Initialize();

	// パーティクル等がシーンのタイムスケールを参照できるようにする
	EngineTime::SetProvider(currentScene_.get());
}

void SceneManager::ChangeScene(const std::string& sceneName, TransitionType transitionType) {
	if (IsTransitioning()) {
		return;   // 多重遷移を防ぐ
	}

	// 画面が覆われた瞬間に実際の差し替えが走る
	TransitionManager::GetInstance()->StartTransition(
		transitionType, currentSceneName_, sceneName,
		[this, sceneName]() { ApplyScene(sceneName); });
}

void SceneManager::ChangeSceneImmediate(const std::string& sceneName) {
	ApplyScene(sceneName);
}

bool SceneManager::IsTransitioning() const {
	return TransitionManager::GetInstance()->IsTransitioning();
}

void SceneManager::Update() {
	TransitionManager::GetInstance()->Update();

	if (currentScene_) {
		currentScene_->ReportProfileGauges();
		currentScene_->Update();
	}
}

void SceneManager::Draw() {
	if (currentScene_) {
		currentScene_->Draw();
	}
}

void SceneManager::DrawTransition() {
	TransitionManager::GetInstance()->Draw();
}

void SceneManager::Finalize() {
	EngineTime::SetProvider(nullptr);

	if (currentScene_) {
		currentScene_->Finalize();
		currentScene_.reset();
	}
	TransitionManager::GetInstance()->Finalize();
}
