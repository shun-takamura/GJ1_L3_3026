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
#include "Match/MatchScoreText.h"
#include "Sound/SoundManager.h"

#include <cmath>
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
	// 背景。カメラの正面方向へ一定距離だけ進めた位置に Plane を置き、タイトル画面
	// (アトラクトモードの GameScene)と同じテクスチャを貼る。サイズは kBgDistance での
	// 視錐台の大きさから求め、端が見えないよう少し余裕を持たせる
	// (GameScene::Initialize の背景と同じ考え方)。
	//===================================
	{
		constexpr float kBgDistance = 30.0f;
		constexpr float kBgMargin = 1.1f;

		const Vector3 camPos = camera_->GetTranslate();
		const Vector3 fwd = camera_->GetForward();
		const Vector3 bgPos{
			camPos.x + fwd.x * kBgDistance,
			camPos.y + fwd.y * kBgDistance,
			camPos.z + fwd.z * kBgDistance };

		const float halfHeight = kBgDistance * std::tan(camera_->GetFovY() * 0.5f);
		const float halfWidth = halfHeight * camera_->GetAspectRatio();

		background_ = std::make_unique<PrimitiveInstance>();
		background_->Initialize(PrimitiveInstance::PrimitiveType::Plane, "ResultBackground");
		background_->SetCamera(camera_.get());
		background_->SetTranslate(bgPos);
		background_->SetRotate(camera_->GetRotate());
		background_->SetScale({ halfWidth * 2.0f * kBgMargin, halfHeight * 2.0f * kBgMargin, 1.0f });
		background_->SetTexture("Resources/Textures/Title.dds");
		background_->Update();
	}

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

	//===================================
	// リザルトBGM。LoadFile はプロセス中に1回だけでよい(GameScene::Initialize と同じ
	// static ローカル変数によるガード)。勝敗を問わず常に再生する。
	//===================================
	{
		static bool resultBgmLoaded = false;
		if (!resultBgmLoaded) {
			resultBgmLoaded = true;
			SoundManager::GetInstance()->LoadFile("ResultBGM", "Resources/Sounds/Win/ResultBGM.mp3");
		}
		SoundManager::GetInstance()->Play2DSoundLooped("ResultBGM"); // 曲が終わっても鳴り続ける
	}
}

void ResultScene::Finalize() {
	SoundManager::GetInstance()->Stop2DSound("ResultBGM");
	background_.reset();
	camera_.reset();
}

void ResultScene::Update() {
	UpdateDebugCameraIfActive();
	if (!GetUseDebugCamera()) {
		camera_->Update();
	}

	if (background_) {
		background_->Update();
	}

	// 再生終了検知を毎フレーム進める(08_Audio.md)。
	SoundManager::GetInstance()->Update();

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
	if (background_) {
		background_->Draw();
	}

	auto* tr = TextRenderer::GetInstance();
	if (tr && tr->IsInitialized()) {
		const float w = static_cast<float>(WindowsApplication::kClientWidth);

		float tw = tr->MeasureWidth(winnerText_.c_str(), 2.0f);
		tr->DrawText(winnerText_.c_str(), { (w - tw) * 0.5f, 260.0f }, 2.0f);

		// 勝敗テキストの下に、取ったラウンドを大きく "10 - 3" と出す。
		// その下に、左右どちらがどちらなのかが分かる小さいラベルを添える。
		// 数字とラベルはキャラクターモデルと同じ色(青=プレイヤー / 赤=敵)。
		MatchScoreText::DrawCenteredScore(tr,
			MatchResultRelay::GetPlayerPoints(), MatchResultRelay::GetEnemyPoints(),
			w * 0.5f, 350.0f, 2.4f);
		MatchScoreText::DrawCenteredPair(tr, "PLAYER", "ENEMY", w * 0.5f, 430.0f, 0.8f);

		const char* guide = "Press SPACE or (A)";
		float gw = tr->MeasureWidth(guide, 1.0f);
		tr->DrawText(guide, { (w - gw) * 0.5f, 640.0f }, 1.0f);

		tr->Flush();
	}
}
