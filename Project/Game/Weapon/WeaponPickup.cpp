#include "WeaponPickup.h"

#include <utility>

#include "Camera.h"
#include "Weapon.h"

WeaponPickup::WeaponPickup() = default;
WeaponPickup::~WeaponPickup() = default;

void WeaponPickup::Initialize(Camera* camera, const Vector3& position, std::unique_ptr<Weapon> weapon) {
	position_ = position;
	weapon_ = std::move(weapon);

	// 見た目は仮の小さい箱(地面に置かれた武器のプレースホルダー)。
	visual_ = std::make_unique<PrimitiveInstance>();
	visual_->Initialize(PrimitiveInstance::PrimitiveType::Box, "WeaponPickup_" + weapon_->GetName());
	visual_->SetCamera(camera);
	visual_->SetScale({ 0.5f, 0.3f, 0.5f });
	visual_->SetTranslate(position_);
	visual_->Update();
}

void WeaponPickup::Finalize() {
	visual_.reset();
}

void WeaponPickup::Update() {
	// 位置自体は動かないが、カメラが動く(デバッグカメラ等)場合にも正しく描画されるよう、
	// 毎フレーム WVP を再計算させておく(Character/StageGrid::Tile と同じ考え方)。
	if (visual_) {
		visual_->Update();
	}
}

void WeaponPickup::Draw() {
	if (visual_ && weapon_) {
		visual_->Draw();
	}
}

std::unique_ptr<Weapon> WeaponPickup::TakeWeapon() {
	visual_.reset(); // 取られたら見た目もその場で消す
	return std::move(weapon_);
}
