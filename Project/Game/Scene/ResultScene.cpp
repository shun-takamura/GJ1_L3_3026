#include "ResultScene.h"

#include "SceneManager.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "ControllerInput.h"
#include "Object3DManager.h"
#include "LightManager.h"
#include "TextRenderer.h"
#include "WindowsApplication.h"
#include "Match/MatchResultRelay.h"

#include <cstdio>
#include <dinput.h>
#include <Xinput.h>

void ResultScene::Initialize() {
	//===================================
	// カメラ・ライト(TitleScene と同じ最小セットアップ)
	//===================================
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 1.5f, -7.0f });
	camera_->SetRotate({ 0.12f, 0.0f, 0.0f });
	camera_->Update();

	if (object3DManager_) {
		object3DManager_->SetDefaultCamera(camera_.get());
	}

	auto* lm = LightManager::GetInstance();
	lm->SetDirectionalLightDirection({ -0.3f, -1.0f, 0.4f });
	lm->SetDirectionalLightColor({ 1.0f, 0.98f, 0.92f, 1.0f });
	lm->SetDirectionalLightIntensity(1.2f);

	//===================================
	// 勝者テキスト(GameScene が決着直前に MatchResultRelay へ残したもの)
	//===================================
	switch (MatchResultRelay::GetWinner()) {
	case MatchRule::Winner::Player:
		winnerText_ = "Player Wins!";
		break;
	case MatchRule::Winner::Enemy:
		winnerText_ = "Enemy Wins!";
		break;
	default:
		winnerText_ = "Draw";
		break;
	}
}

void ResultScene::Finalize() {
	camera_.reset();
}

void ResultScene::Update() {
	UpdateDebugCameraIfActive();
	if (!GetUseDebugCamera()) {
		camera_->Update();
	}

	//===================================
	// 入力でタイトルへ戻る(TitleScene::Update と同じ入力)
	//===================================
	bool back = false;
	if (input_) {
		if (auto* kb = input_->GetKeyboard()) {
			back |= kb->TriggerKey(DIK_SPACE) || kb->TriggerKey(DIK_RETURN);
		}
		if (auto* pad = input_->GetController()) {
			back |= pad->IsButtonTriggered(XINPUT_GAMEPAD_A);
		}
	}

	if (back) {
		SceneManager::GetInstance()->ChangeScene("Title", TransitionType::Fade);
	}
}

void ResultScene::Draw() {
	auto* tr = TextRenderer::GetInstance();
	if (tr && tr->IsInitialized()) {
		const float w = static_cast<float>(WindowsApplication::kClientWidth);

		float tw = tr->MeasureWidth(winnerText_.c_str(), 2.0f);
		tr->DrawText(winnerText_.c_str(), { (w - tw) * 0.5f, 260.0f }, 2.0f);

		char pointLine[64];
		snprintf(pointLine, sizeof(pointLine), "Player %d - %d Enemy",
			MatchResultRelay::GetPlayerPoints(), MatchResultRelay::GetEnemyPoints());
		float pw = tr->MeasureWidth(pointLine, 1.2f);
		tr->DrawText(pointLine, { (w - pw) * 0.5f, 340.0f }, 1.2f);

		const char* guide = "Press SPACE or (A)";
		float gw = tr->MeasureWidth(guide, 1.0f);
		tr->DrawText(guide, { (w - gw) * 0.5f, 640.0f }, 1.0f);

		tr->Flush();
	}
}
