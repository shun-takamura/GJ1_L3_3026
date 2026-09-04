#include "WeaponPickup.h"

#include <utility>

#include "Camera.h"
#include "Weapon.h"
#include "Common/IStageQuery.h"
#include "Object3DInstance.h"

namespace {
	// 地面に置いた武器モデルの見た目調整。実機で武器モデルの実寸を見てから詰める。
	constexpr float kModelScale = 1.0f;
	constexpr float kModelYOffset = 0.3f;   // 地面(セル境界)に少し浮かせて置く
	constexpr float kSpinSpeed = 1.2f;      // rad/frame ではなく rad/更新。ゆっくり回して目を引く
}

WeaponPickup::WeaponPickup() = default;
WeaponPickup::~WeaponPickup() = default;

void WeaponPickup::Initialize(Camera* camera, Object3DManager* object3DManager, DirectXCore* dxCore,
	const Vector3& position, std::unique_ptr<Weapon> weapon, const IStageQuery* stage) {
	position_ = position;
	weapon_ = std::move(weapon);
	stage_ = stage;
	verticalVelocity_ = 0.0f;
	grounded_ = false;

	// まず実際の武器モデルの読み込みを試みる。
	const std::string dir = weapon_->GetModelDirectory();
	const std::string file = weapon_->GetModelFileName();
	if (object3DManager && dxCore && !dir.empty() && !file.empty()) {
		model_ = std::make_unique<Object3DInstance>();
		model_->Initialize(object3DManager, dxCore, dir, file, "WeaponPickup_" + weapon_->GetName());
		model_->SetCamera(camera);
		model_->SetScale({ kModelScale, kModelScale, kModelScale });
		model_->SetTranslate({ position_.x, position_.y + kModelYOffset, position_.z });
		model_->Update();
	}

	// モデルが用意できなかったときだけ、仮の小さい箱で代替する。
	if (!model_) {
		visual_ = std::make_unique<PrimitiveInstance>();
		visual_->Initialize(PrimitiveInstance::PrimitiveType::Box, "WeaponPickup_" + weapon_->GetName());
		visual_->SetCamera(camera);
		visual_->SetScale({ 0.5f, 0.3f, 0.5f });
		visual_->SetTranslate(position_);
		visual_->Update();
	}
}

void WeaponPickup::Finalize() {
	model_.reset();
	visual_.reset();
}

void WeaponPickup::Update(float dt) {
	// Character::Update と同じ考え方(重力を積分 → MoveAabb で地形とめり込み解消)を毎フレーム行う。
	// 着地後も止めないのがポイント: 一度着地したら二度と地形を見ない実装だと、着地後に
	// 足場のブロックが破壊された場合に検知できず宙に浮いたまま残ってしまう(実際に報告された
	// 不具合)。投げ捨てた武器がブロックの側面や真下に床が無い位置で着弾しても、ここで実際に
	// 足場がある高さまで落ちてくれるので「壁に引っかかったまま浮く」こともなくなる。
	if (stage_) {
		verticalVelocity_ += kGravity * dt;
		const Vector3 to{ position_.x, position_.y + verticalVelocity_ * dt, position_.z };
		const StageMoveResult result = stage_->MoveAabb(position_, to, kHalfExtent);
		position_ = result.position;
		if (result.grounded) {
			grounded_ = true;
			verticalVelocity_ = 0.0f;
		} else {
			grounded_ = false; // 足場が壊れるなどして接地が外れたら、ここで再び落下を再開する
		}
	}

	// 位置が動かない(着地済みの)間も、カメラが動く(デバッグカメラ等)場合に正しく描画されるよう、
	// 毎フレーム WVP を再計算させておく(Character/StageGrid::Tile と同じ考え方)。
	if (model_) {
		spinAngle_ += kSpinSpeed * (1.0f / 60.0f);
		model_->SetRotate({ 0.0f, spinAngle_, 0.0f });
		model_->SetTranslate({ position_.x, position_.y + kModelYOffset, position_.z });
		model_->Update();
	}
	if (visual_) {
		visual_->SetTranslate(position_);
		visual_->Update();
	}
}

void WeaponPickup::Draw() {
	if (visual_ && weapon_) {
		visual_->Draw();
	}
}

void WeaponPickup::DrawModel(DirectXCore* dxCore) {
	if (model_ && weapon_) {
		model_->Draw(dxCore);
	}
}

std::unique_ptr<Weapon> WeaponPickup::TakeWeapon() {
	model_.reset();  // 取られたら見た目もその場で消す
	visual_.reset();
	return std::move(weapon_);
}
