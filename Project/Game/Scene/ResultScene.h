#pragma once

#include <memory>
#include <string>

#include "Scene.h"
#include "Camera.h"

/// <summary>
/// 1セット(10ポイント先取)決着後に一瞬挟む結果画面。
///
/// GameScene が決着直前に MatchResultRelay::SetWinner() で勝者を渡してからこのシーンへ遷移する。
/// Space/Enter/(A) で Title へ戻る(TitleScene::Update と同じ入力)。
/// </summary>
class ResultScene : public Scene {
public:
	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override { return camera_.get(); }

private:
	std::unique_ptr<Camera> camera_;

	// Initialize 時に MatchResultRelay::GetWinner() から作る表示用文字列("Player Wins!" 等)。
	std::string winnerText_;
};
