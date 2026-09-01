#pragma once

#include <memory>

#include "Framework.h"

class ISceneRunner;
class SceneFactory;
class GPUParticleManager;
class RenderTexture;

/// <summary>
/// ゲーム本体のアプリクラス。**これをコピーして自分のゲームを作る。**
///
/// Framework（エンジンのアプリ骨格）が DirectX12 の初期化・ウィンドウ・入力・
/// 各マネージャ・ImGui をすべて用意するので、アプリ側が書くのは
///   - Draw() の組み立て
///   - シーン駆動の実体を GetSceneRunner() で返すこと
///   - 各種フックの配線
/// の3つだけ。
///
/// シーンを増やすときは Scene/SceneFactory.cpp に1行足す。
/// </summary>
class GameApp : public Framework {
public:
	GameApp();
	~GameApp() override;

	/// <summary>ウィンドウのタイトルバーに出す文字列。自分のゲーム名に変える。</summary>
	const wchar_t* GetWindowTitle() const override { return L"My Game"; }

	void Initialize() override;
	void Finalize() override;
	void Draw() override;

	/// <summary>シーン駆動は SceneManager に委ねる（依存性の逆転）。</summary>
	ISceneRunner* GetSceneRunner() override;

	static GameApp* GetInstance() { return instance_; }

private:
	static GameApp* instance_;

	std::unique_ptr<SceneFactory> sceneFactory_;

	// エフェクト系（EffectManager が参照するので Framework より長生きさせる）
	std::unique_ptr<GPUParticleManager> gpuParticleManager_;

#ifdef _DEBUG
	// Debug ビルド専用: ImGui の Scene ビューポートに表示する描画先
	std::unique_ptr<RenderTexture> viewportRenderTexture_;
#endif
};
