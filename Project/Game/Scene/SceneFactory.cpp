#include "SceneFactory.h"

#include "GameScene.h"
#include "ResultScene.h"

std::unique_ptr<Scene> SceneFactory::CreateScene(const std::string& sceneName) {
	// ここに追加していく。名前は SceneManager::ChangeScene に渡すものと揃える
	if (sceneName == "Title") {
		// タイトル画面は「敵 AI 同士が Sample ステージで戦い続けるデモプレイ」。
		// 専用の TitleScene は持たず、アトラクトモードにした GameScene をそのまま使う
		// (SPACE / Enter / (A) で本編の Game シーンへ入る)。
		auto scene = std::make_unique<GameScene>();
		scene->SetAttractMode(true);
		return scene;
	}
	if (sceneName == "Game") {
		return std::make_unique<GameScene>();
	}
	if (sceneName == "Result") {
		return std::make_unique<ResultScene>();
	}

	// 未知の名前。SceneManager 側で assert に落ちる
	return nullptr;
}
