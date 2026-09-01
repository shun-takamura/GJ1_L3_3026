#include "SceneFactory.h"

#include "TitleScene.h"
#include "GameScene.h"

std::unique_ptr<Scene> SceneFactory::CreateScene(const std::string& sceneName) {
	// ここに追加していく。名前は SceneManager::ChangeScene に渡すものと揃える
	if (sceneName == "Title") {
		return std::make_unique<TitleScene>();
	}
	if (sceneName == "Game") {
		return std::make_unique<GameScene>();
	}

	// 未知の名前。SceneManager 側で assert に落ちる
	return nullptr;
}
