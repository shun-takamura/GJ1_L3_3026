#include "GameScene.h"

#include "SceneManager.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "ControllerInput.h"
#include "Object3DManager.h"
#include "LightManager.h"
#include "TextRenderer.h"
#include "WindowsApplication.h"
#include "TimeGroup.h"
#include "Primitive/DebugDraw.h"
#include "Primitive/LineRenderer.h"

#include <dinput.h>
#include <Xinput.h>

namespace {
	constexpr float kMoveSpeed = 6.0f;
}

void GameScene::Initialize() {
	//===================================
	// カメラ
	//===================================
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 6.0f, -12.0f });
	camera_->SetRotate({ 0.42f, 0.0f, 0.0f });
	camera_->Update();

	if (object3DManager_) {
		object3DManager_->SetDefaultCamera(camera_.get());
	}

	//===================================
	// ライト
	//===================================
	auto* lm = LightManager::GetInstance();
	lm->SetDirectionalLightDirection({ -0.4f, -1.0f, 0.3f });
	lm->SetDirectionalLightColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	lm->SetDirectionalLightIntensity(1.0f);

	//===================================
	// 床
	//===================================
	ground_ = std::make_unique<PrimitiveInstance>();
	ground_->Initialize(PrimitiveInstance::PrimitiveType::Box, "Ground");
	ground_->SetCamera(camera_.get());
	ground_->SetTranslate({ 0.0f, -0.25f, 0.0f });
	ground_->SetScale({ 20.0f, 0.5f, 20.0f });

	//===================================
	// 操作対象
	//===================================
	player_ = std::make_unique<PrimitiveInstance>();
	player_->Initialize(PrimitiveInstance::PrimitiveType::Box, "Player");
	player_->SetCamera(camera_.get());
	player_->SetTranslate(playerPos_);
}

void GameScene::Finalize() {
	player_.reset();
	ground_.reset();
	camera_.reset();
}

void GameScene::Update() {
	UpdateDebugCameraIfActive();
	if (!GetUseDebugCamera()) {
		camera_->Update();
	}

	// ゲームロジックは Player グループの時間で進める。
	// ヒットストップやスローを入れるときにここが効く
	const float dt = GetScaledDeltaTime(TimeGroup::Player);

	//===================================
	// 移動
	//===================================
	Vector3 move{ 0.0f, 0.0f, 0.0f };
	if (input_) {
		if (auto* kb = input_->GetKeyboard()) {
			if (kb->PushKey(DIK_A)) move.x -= 1.0f;
			if (kb->PushKey(DIK_D)) move.x += 1.0f;
			if (kb->PushKey(DIK_W)) move.z += 1.0f;
			if (kb->PushKey(DIK_S)) move.z -= 1.0f;
		}
		if (auto* pad = input_->GetController()) {
			auto ls = pad->GetLeftStick();
			if (ls.magnitude > 0.0f) {
				move.x += ls.x * ls.magnitude;
				move.z += ls.y * ls.magnitude;
			}
		}
	}

	playerPos_.x += move.x * kMoveSpeed * dt;
	playerPos_.z += move.z * kMoveSpeed * dt;

	if (player_) {
		player_->SetTranslate(playerPos_);
		player_->Update();
	}
	if (ground_) {
		ground_->Update();
	}

	//===================================
	// タイトルへ戻る
	//===================================
	bool back = false;
	if (input_) {
		if (auto* kb = input_->GetKeyboard()) {
			back |= kb->TriggerKey(DIK_ESCAPE);
		}
		if (auto* pad = input_->GetController()) {
			back |= pad->IsButtonTriggered(XINPUT_GAMEPAD_B);
		}
	}
	if (back) {
		SceneManager::GetInstance()->ChangeScene("Title", TransitionType::Fade);
	}
}

void GameScene::Draw() {
	if (ground_) ground_->Draw();
	if (player_) player_->Draw();

	//===================================
	// デバッグ線の描画。
	// DebugDraw::* は線をキューに積むだけなので、
	// 最後に LineRenderer::Draw() を呼ばないと何も出ない。
	//===================================
	DebugDraw::Grid({ 0.0f, 0.01f, 0.0f }, 20.0f, 1.0f, { 0.4f, 0.4f, 0.5f, 1.0f });

	auto* lr = LineRenderer::GetInstance();
	lr->SetCamera(GetCamera());
	lr->Draw();

	auto* tr = TextRenderer::GetInstance();
	if (tr && tr->IsInitialized()) {
		tr->DrawText("WASD / Left Stick : Move", { 32.0f, 32.0f }, 0.8f);
		tr->DrawText("ESC / (B) : Title", { 32.0f, 64.0f }, 0.8f);
		tr->Flush();
	}
}
