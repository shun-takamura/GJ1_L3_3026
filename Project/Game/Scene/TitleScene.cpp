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
	// 飾りのキューブ
	//===================================
	logo_ = std::make_unique<PrimitiveInstance>();
	logo_->Initialize(PrimitiveInstance::PrimitiveType::Box, "TitleLogo");
	logo_->SetCamera(camera_.get());
	logo_->SetScale({ 1.6f, 1.6f, 1.6f });
}

void TitleScene::Finalize() {
	logo_.reset();
	camera_.reset();
}

void TitleScene::Update() {
	UpdateDebugCameraIfActive();
	if (!GetUseDebugCamera()) {
		camera_->Update();
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
