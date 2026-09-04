#include "FireHazard.h"

#include <cmath>

#include "Camera.h"
#include "Vector4.h"

FireHazard::FireHazard() = default;
FireHazard::~FireHazard() = default;

void FireHazard::Initialize(Camera* camera, const Vector3& position, float radius, float dps, float duration) {
	position_ = position;
	radius_ = radius;
	dps_ = dps;
	remaining_ = duration;

	// 見た目は半径ぶん平たく引き伸ばした箱1枚(専用モデルは無いので、他の実体と同じく
	// プリミティブフォールバックのみで表現する。WeaponPickup/ArcingProjectile 参照)。
	// 縦(Y)を薄くすることで、地面に置いた炎の床のように見せている。
	visual_ = std::make_unique<PrimitiveInstance>();
	visual_->Initialize(PrimitiveInstance::PrimitiveType::Box, "FireHazard");
	visual_->SetCamera(camera);
	visual_->SetScale({ radius_ * 2.0f, 0.08f, radius_ * 2.0f });
	visual_->SetTranslate(position_);
	// Blaster の爆風デバッグ表示(橙)と近い配色に揃えて、「炎銃系」と一目で分かるようにする。
	visual_->GetMesh().SetColor(Vector4{ 0.9f, 0.3f, 0.05f, 1.0f });
	visual_->Update();
}

void FireHazard::Finalize() {
	visual_.reset();
}

void FireHazard::Update(float dt) {
	remaining_ -= dt;
	// 位置は動かないが、他の実体と同じく毎フレーム Update() して WVP を再計算させておく
	// (デバッグカメラ等でカメラ側が動くケースに対応するため。WeaponPickup と同じ考え方)。
	if (visual_) {
		visual_->Update();
	}
}

void FireHazard::Draw() {
	if (visual_) {
		visual_->Draw();
	}
}

bool FireHazard::Overlaps(const Vector3& pos) const {
	const float dx = pos.x - position_.x;
	const float dy = pos.y - position_.y;
	return std::sqrt(dx * dx + dy * dy) <= radius_;
}
