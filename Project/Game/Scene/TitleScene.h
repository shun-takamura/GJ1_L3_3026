#pragma once

#include <memory>

#include "Scene.h"
#include "Camera.h"
#include "Primitive/PrimitiveInstance.h"

/// <summary>
/// タイトル画面。Space かゲームパッドの A で Game シーンへ遷移する。
///
/// シーンを作るときの最小構成の見本。やることは4つ。
///   Initialize / Finalize / Update / Draw を実装し、GetCamera を override する。
/// </summary>
class TitleScene : public Scene {
public:
	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override { return camera_.get(); }

private:
	std::unique_ptr<Camera> camera_;

	// タイトル画面の背景。奥に置いた大きな Plane にテクスチャを貼って表現する
	// (SpriteInstance は深度を無視するスクリーン座標描画なので、3Dの飾りキューブより
	// 手前に出てしまう。3D の一部として奥へ置けば通常の深度テストで正しく隠れる)。
	std::unique_ptr<PrimitiveInstance> background_;

	// タイトルの飾り。ゆっくり回るキューブ
	std::unique_ptr<PrimitiveInstance> logo_;
	float spin_ = 0.0f;
};
