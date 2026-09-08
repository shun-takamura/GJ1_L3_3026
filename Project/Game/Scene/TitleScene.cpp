#include "TitleScene.h"

#include "SceneManager.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "ControllerInput.h"
#include "Object3DManager.h"
#include "LightManager.h"
#include "TextRenderer.h"
#include "WindowsApplication.h"
#include "TimeGroup.h"

#include <dinput.h>
#include <Xinput.h>

void TitleScene::Initialize() {
	//===================================
	// カメラ
	//===================================
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 1.5f, -7.0f });
	camera_->SetRotate({ 0.12f, 0.0f, 0.0f });
	camera_->Update();

	if (object3DManager_) {
		object3DManager_->SetDefaultCamera(camera_.get());
	}

	//===================================
	// ライト。設定しないと真っ暗になるので必ず入れる
	//===================================
	auto* lm = LightManager::GetInstance();
	lm->SetDirectionalLightDirection({ -0.3f, -1.0f, 0.4f });
	lm->SetDirectionalLightColor({ 1.0f, 0.98f, 0.92f, 1.0f });
	lm->SetDirectionalLightIntensity(1.2f);

	//===================================
	// 背景。カメラの正面方向へ一定距離だけ進めた位置に Plane を置いてテクスチャを貼る。
	// カメラと同じ向きに回転させることで、ピッチが付いていても画面いっぱいの
	// スクリーンに正対した板になる(サイズは distance でのカメラ視錐台の大きさに
	// 合わせた概算値。fovY≈0.45rad, 16:9 のときの計算値に少し余裕を持たせている)。
	//===================================
	{
		constexpr float kBgDistance = 15.0f;
		const Vector3 camPos = camera_->GetTranslate();
		const Vector3 fwd = camera_->GetForward();
		const Vector3 bgPos{
			camPos.x + fwd.x * kBgDistance,
			camPos.y + fwd.y * kBgDistance,
			camPos.z + fwd.z * kBgDistance };

		background_ = std::make_unique<PrimitiveInstance>();
		background_->Initialize(PrimitiveInstance::PrimitiveType::Plane, "TitleBackground");
		background_->SetCamera(camera_.get());
		background_->SetTranslate(bgPos);
		background_->SetRotate(camera_->GetRotate());
		background_->SetScale({ 14.0f, 8.0f, 1.0f });
		background_->SetTexture("Resources/Textures/Title.dds");
	}

	//===================================
	// 飾りのキューブ
	//===================================
	logo_ = std::make_unique<PrimitiveInstance>();
	logo_->Initialize(PrimitiveInstance::PrimitiveType::Box, "TitleLogo");
	logo_->SetCamera(camera_.get());
	logo_->SetScale({ 1.6f, 1.6f, 1.6f });
}

void TitleScene::Finalize() {
	logo_.reset();
	background_.reset();
	camera_.reset();
}

void TitleScene::Update() {
	UpdateDebugCameraIfActive();
	if (!GetUseDebugCamera()) {
		camera_->Update();
	}

	if (background_) {
		background_->Update();
	}

	// UI グループの時間で回す。ポーズしても回り続けてほしいので World ではない
	spin_ += GetScaledDeltaTime(TimeGroup::UI) * 0.6f;
	if (logo_) {
		logo_->SetRotate({ 0.3f, spin_, 0.0f });
		logo_->Update();
	}

	//===================================
	// 入力でシーン遷移
	//===================================
	bool start = false;
	if (input_) {
		if (auto* kb = input_->GetKeyboard()) {
			start |= kb->TriggerKey(DIK_SPACE) || kb->TriggerKey(DIK_RETURN);
		}
		if (auto* pad = input_->GetController()) {
			start |= pad->IsButtonTriggered(XINPUT_GAMEPAD_A);
		}
	}

	if (start) {
		// フェードで Game シーンへ。画面が覆われた瞬間に切り替わる
		SceneManager::GetInstance()->ChangeScene("Game", TransitionType::Fade);
	}
}

void TitleScene::Draw() {
	if (background_) {
		background_->Draw();
	}

	if (logo_) {
		logo_->Draw();
	}

	//===================================
	// 文字
	//===================================
	auto* tr = TextRenderer::GetInstance();
	if (tr && tr->IsInitialized()) {
		const float w = static_cast<float>(WindowsApplication::kClientWidth);

		const char* title = "ArcanaEngine";
		float tw = tr->MeasureWidth(title, 2.0f);
		tr->DrawText(title, { (w - tw) * 0.5f, 120.0f }, 2.0f);

		const char* guide = "Press SPACE or (A)";
		float gw = tr->MeasureWidth(guide, 1.0f);
		tr->DrawText(guide, { (w - gw) * 0.5f, 640.0f }, 1.0f);

		tr->Flush();
	}
}
