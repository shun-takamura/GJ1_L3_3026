#pragma once

#include <memory>
#include <string>

#include "ISceneRunner.h"
#include "Transition/BaseTransition.h"

class Scene;
class AbstractSceneFactory;
class SpriteManager;
class Object3DManager;
class SkyboxManager;
class DirectXCore;
class SRVManager;
class InputManager;
class SkinningComputeManager;

/// <summary>
/// シーンのステート管理。エンジンの Framework からは ISceneRunner 経由で駆動される。
///
/// 遷移はステートパターン: 「今のシーン」を1つだけ持ち、ChangeScene で差し替える。
/// トランジション付きの場合は画面が覆われた瞬間に実際の差し替えが起きる。
///
/// シーンの実体生成は AbstractSceneFactory に委ねる（SceneFactory.cpp を参照）。
/// シーンを増やすときは
///   1. Scene 派生クラスを作る
///   2. SceneFactory::CreateScene に名前を追加する
/// の2つだけでよい。
/// </summary>
class SceneManager : public ISceneRunner {
public:
	static SceneManager* GetInstance();

	//====================
	// ISceneRunner
	//====================
	void Initialize(
		SpriteManager* spriteManager,
		Object3DManager* object3DManager,
		SkyboxManager* skyboxManager,
		DirectXCore* dxCore,
		SRVManager* srvManager,
		InputManager* input,
		SkinningComputeManager* skinningComputeManager) override;

	void Update() override;
	void Finalize() override;

	/// <summary>現在のシーンを描画する。アプリの Draw から呼ぶ。</summary>
	void Draw();

	/// <summary>トランジション（画面を覆う演出）を描画する。シーン描画のあとに呼ぶ。</summary>
	void DrawTransition();

	//====================
	// シーン遷移
	//====================

	/// <summary>トランジション付きでシーンを変更する。</summary>
	void ChangeScene(const std::string& sceneName, TransitionType transitionType);

	/// <summary>トランジションなしで即座に切り替える。</summary>
	void ChangeSceneImmediate(const std::string& sceneName);

	/// <summary>最初のシーンを設定する（初期化時に一度だけ）。</summary>
	void SetStartScene(const std::string& sceneName) { startSceneName_ = sceneName; }

	void SetSceneFactory(AbstractSceneFactory* factory) { sceneFactory_ = factory; }

	Scene* GetCurrentScene() const { return currentScene_.get(); }
	const std::string& GetCurrentSceneName() const { return currentSceneName_; }
	bool IsTransitioning() const;

private:
	// currentScene_ が前方宣言のみの Scene を指すため、実体は .cpp に置く
	SceneManager();
	~SceneManager();
	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

	/// <summary>シーンを実際に生成して差し替える。</summary>
	void ApplyScene(const std::string& sceneName);

	std::unique_ptr<Scene> currentScene_;
	std::string currentSceneName_;
	std::string startSceneName_ = "Title";

	AbstractSceneFactory* sceneFactory_ = nullptr;

	// Scene 基底へ注入するマネージャ群
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	SkyboxManager* skyboxManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	InputManager* input_ = nullptr;
	SkinningComputeManager* skinningComputeManager_ = nullptr;
};
