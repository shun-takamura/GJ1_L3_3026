#include "ArcingProjectile.h"

#include "Camera.h"
#include "Character/Character.h"
#include "Common/IStageQuery.h"
#include "Weapon.h"

namespace {
	// Character::kGravity と同じ値。世界の重力を1箇所にまとめられないか迷ったが、
	// private static constexpr を跨いで共有する手段が無いため、値を合わせて重複させている。
	// ここを変えるときは Character.h の kGravity も合わせて変えること。
	constexpr float kGravityAcceleration = -20.0f;
}

ArcingProjectile::ArcingProjectile() = default;
ArcingProjectile::~ArcingProjectile() = default;

void ArcingProjectile::Initialize(Camera* camera, const std::string& name, const ProjectileSpawnRequest& spec,
	Character* owner, const IStageQuery* stage,
	PrimitiveInstance::PrimitiveType visualType, const Vector3& visualScale) {
	camera_ = camera;
	stage_ = stage;
	owner_ = owner;

	position_ = spec.origin;
	velocityX_ = spec.velocityX;
	velocityY_ = spec.velocityY;
	gravityScale_ = spec.gravityScale;
	radius_ = spec.radius;
	lifeTimer_ = spec.lifeTime;
	damage_ = spec.damage;
	knockbackPower_ = spec.knockbackPower;

	visual_ = std::make_unique<PrimitiveInstance>();
	visual_->Initialize(visualType, name);
	visual_->SetCamera(camera_);
	visual_->SetScale(visualScale);
	visual_->SetTranslate(position_);
}

void ArcingProjectile::Finalize() {
	visual_.reset();
}

void ArcingProjectile::Update(float dt) {
	if (dead_) {
		return;
	}

	lifeTimer_ -= dt;
	if (lifeTimer_ <= 0.0f) {
		dead_ = true; // 寿命切れ。何にも当たらないまま静かに消える
		return;
	}

	// Character の重力積分(verticalVelocity_ += kGravity * dt)と同じ考え方で、
	// 初速+重力による放物線を毎フレーム積分する。
	velocityY_ += kGravityAcceleration * gravityScale_ * dt;
	position_.x += velocityX_ * dt;
	position_.y += velocityY_ * dt;

	// ---- 地形との当たり判定 ----
	// 小さい飛翔体を「半径radius_の正方形」とみなした簡易判定(正確な球判定ではないが、
	// 見た目のサイズが小さいので視覚的な誤差は無視できる)。ステージ範囲外(場外)に
	// 出た場合も「何にも当たらず消える」という点では地形衝突と同じ扱いにしている。
	// フェーズ3の壊れる床は Character の攻撃(GameScene::ResolveAttack)と同様、
	// 必要になったら GameScene 側で StageGrid::DamageSphere を呼んで削る想定。
	if (stage_) {
		const Vector3 half{ radius_, radius_, radius_ };
		if (stage_->OverlapsSolid(position_, half) || !stage_->IsPointInsideBounds(position_)) {
			dead_ = true;
			diedOnTerrain_ = true;
			return;
		}
	}

	if (visual_) {
		visual_->SetTranslate(position_);
		visual_->Update();
	}
}

void ArcingProjectile::Draw() {
	if (visual_ && !dead_) {
		visual_->Draw();
	}
}

void ArcingProjectile::TryHitCharacter(Character& defender) {
	if (dead_ || &defender == owner_) {
		return; // 既に消滅済み、または発射者自身には当てない
	}

	Character::AttackHitbox hitbox;
	hitbox.center = position_;
	hitbox.radius = radius_;
	hitbox.damage = damage_;
	hitbox.knockbackPower = knockbackPower_;
	// ノックバックは「飛んでいる方向」をそのまま使う(Character::AttackHitbox.knockbackDirX と
	// 同じ考え方: 命中位置から逆算すると密着距離で符号が反転するバグになるため)。
	hitbox.knockbackDirX = (velocityX_ >= 0.0f) ? 1.0f : -1.0f;

	if (defender.ReceiveHit(hitbox)) {
		dead_ = true; // 命中したので消える(貫通はしない)
	}
}

void ArcingProjectile::SetThrownWeaponPayload(std::unique_ptr<Weapon> weapon) {
	thrownWeaponPayload_ = std::move(weapon);
}

std::unique_ptr<Weapon> ArcingProjectile::TakeThrownWeaponPayload() {
	return std::move(thrownWeaponPayload_);
}
