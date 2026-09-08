#pragma once

#include <memory>

#include "Framework.h"

class ISceneRunner;
class SceneFactory;
class GPUParticleManager;
class RenderTexture;
class PostEffect;

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
	const wchar_t* GetWindowTitle() const override { return L"3026_BREAKP01NT"; }

	void Initialize() override;
	void Finalize() override;
	void Draw() override;

	/// <summary>シーン駆動は SceneManager に委ねる（依存性の逆転）。</summary>
	ISceneRunner* GetSceneRunner() override;

	static GameApp* GetInstance() { return instance_; }

	/// <summary>ImGui の getPostEffect フック配線用。未初期化なら nullptr。</summary>
	static PostEffect* GetPostEffect() { return instance_ ? instance_->postEffect_.get() : nullptr; }

private:
	static GameApp* instance_;

	std::unique_ptr<SceneFactory> sceneFactory_;

	// エフェクト系（EffectManager が参照するので Framework より長生きさせる）
	std::unique_ptr<GPUParticleManager> gpuParticleManager_;

	// シーンを一度 RenderTexture に描いてからフィルタ合成する（ポータルの Warp 歪み等）。
	std::unique_ptr<PostEffect> postEffect_;

	/// <summary>
	/// シーンを postEffect_ の RT へ描き、歪みパスを挟んでフィルタ合成し、output へ出す。
	/// output=nullptr でスワップチェーン（この場合は関数内で dxCore_->BeginDraw する）。
	/// </summary>
	void RenderSceneWithPostEffect(RenderTexture* output);

#ifdef _DEBUG
	// Debug ビルド専用: ImGui の Scene ビューポートに表示する描画先
	std::unique_ptr<RenderTexture> viewportRenderTexture_;
#endif
};
