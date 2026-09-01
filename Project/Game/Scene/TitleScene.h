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

	// タイトルの飾り。ゆっくり回るキューブ
	std::unique_ptr<PrimitiveInstance> logo_;
	float spin_ = 0.0f;
};
