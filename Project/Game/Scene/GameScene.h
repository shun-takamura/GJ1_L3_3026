#pragma once

#include <memory>
#include <vector>

#include "Scene.h"
#include "Camera.h"
#include "Primitive/PrimitiveInstance.h"

/// <summary>
/// ゲーム本編の雛形。
///
/// 操作できるキューブを1つ置いてある。ここを起点に作り始める想定。
/// ESC / (B) でタイトルへ戻る。
/// </summary>
class GameScene : public Scene {
public:
	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override { return camera_.get(); }

private:
	std::unique_ptr<Camera> camera_;

	// 操作対象
	std::unique_ptr<PrimitiveInstance> player_;
	Vector3 playerPos_{ 0.0f, 0.5f, 0.0f };

	// 床
	std::unique_ptr<PrimitiveInstance> ground_;
};
