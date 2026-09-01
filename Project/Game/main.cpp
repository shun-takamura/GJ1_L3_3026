#include <memory>

#include "GameApp.h"
#include "CrashHandler.h"

/// <summary>
/// ArcanaEngine 最小サンプルのエントリポイント。
/// アプリ側がやることは Framework 派生を1つ作って Run() を呼ぶだけ。
/// </summary>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	// 未捕捉例外で .dmp を残すハンドラを最初に仕込む
	CrashHandler::Install();

	std::unique_ptr<Framework> app = std::make_unique<GameApp>();
	app->Run();

	return 0;
}
