#pragma once

#include "AbstractSceneFactory.h"

/// <summary>
/// シーン名から実体を作る工場。
///
/// **シーンを増やすときはここに1行足すだけでよい。**
/// SceneManager はこの工場越しにしかシーンを知らないので、
/// シーンクラス同士がお互いを include する必要がない。
/// </summary>
class SceneFactory : public AbstractSceneFactory {
public:
	std::unique_ptr<Scene> CreateScene(const std::string& sceneName) override;
};
