#include "ArcingProjectile.h"

#include <algorithm>
#include <cmath>

#include "Camera.h"
#include "Character/Character.h"
#include "Common/IStageQuery.h"
#include "Weapon.h"
#include "Object3DInstance.h"
#include "Log.h"

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
	PrimitiveInstance::PrimitiveType visualType, const Vector3& visualScale,
	Object3DManager* object3DManager, DirectXCore* dxCore,
	const std::string& modelDir, const std::string& modelFile) {
	camera_ = camera;
	stage_ = stage;
	owner_ = owner;

	origin_ = spec.origin;
	position_ = spec.origin;
	velocityX_ = spec.velocityX;
	velocityY_ = spec.velocityY;
	gravityScale_ = spec.gravityScale;
	radius_ = spec.radius;
	lifeTimer_ = spec.lifeTime;
	damage_ = spec.damage;
	knockbackPower_ = spec.knockbackPower;
	blastRadius_ = spec.blastRadius;
	bounces_ = spec.bounces;
	wallRestitution_ = spec.wallRestitution;
	floorRestitution_ = spec.floorRestitution;
	maxBounces_ = spec.maxBounces;
	bounceCount_ = 0;
	proximityRadius_ = spec.proximityRadius;
	damageFalloffRange_ = spec.damageFalloffRange;
	minDamageMultiplier_ = spec.minDamageMultiplier;

	// 投げ武器でモデル指定があればモデルを、無ければ(＝銃弾)プリミティブを見た目にする。
	if (object3DManager && dxCore && !modelDir.empty() && !modelFile.empty()) {
		// クラッシュ調査用: WeaponPickup.cpp と同じ理由でログを残す。
		Log("ArcingProjectile: loading model name=" + name + " dir=" + modelDir + " file=" + modelFile + "\n");
		model_ = std::make_unique<Object3DInstance>();
		model_->Initialize(object3DManager, dxCore, modelDir, modelFile, name);
		model_->SetCamera(camera_);
		model_->SetScale({ 1.0f, 1.0f, 1.0f });
		model_->SetTranslate(position_);
		model_->Update();
	} else {
		visual_ = std::make_unique<PrimitiveInstance>();
		visual_->Initialize(visualType, name);
		visual_->SetCamera(camera_);
		visual_->SetScale(visualScale);
		visual_->SetTranslate(position_);
	}
}

void ArcingProjectile::Finalize() {
	visual_.reset();
	model_.reset();
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

	if (!bounces_) {
		// ---- 従来どおりの一本道の物理(Pistol/AssaultRifle/Shotgun/Blaster) ----
		// 小さい飛翔体を「半径radius_の正方形」とみなした簡易判定(正確な球判定ではないが、
		// 見た目のサイズが小さいので視覚的な誤差は無視できる)。1フレーム分まとめて移動してから
		// 重なりを見るだけなので、地形へ触れた瞬間にそのまま埋まった位置で死ぬ
		// (跳ね返り武器がこの粗さを避けるために下の MoveAabb 経路を使う理由でもある)。
		position_.x += velocityX_ * dt;
		position_.y += velocityY_ * dt;

		if (stage_) {
			const Vector3 half{ radius_, radius_, radius_ };
			if (stage_->OverlapsSolid(position_, half) || !stage_->IsPointInsideBounds(position_)) {
				dead_ = true;
				diedOnTerrain_ = true;
				return;
			}
		}
	} else {
		// ---- 跳ね返り経路(グレネードランチャー・投げ捨てた武器) ----
		// Character/WeaponPickup と同じ MoveAabb によるスイープ判定で、めり込む前に
		// hitWall/grounded を検出してから速度を反射させる(ProjectileSpawnRequest::bounces
		// のコメント参照)。
		if (!stage_ || !stage_->IsPointInsideBounds(position_)) {
			dead_ = true;
			diedOnTerrain_ = true;
			return;
		}
		const Vector3 half{ radius_, radius_, radius_ };
		const Vector3 to{ position_.x + velocityX_ * dt, position_.y + velocityY_ * dt, position_.z };
		const StageMoveResult mv = stage_->MoveAabb(position_, to, half);
		position_ = mv.position;

		// 反射回数の上限(maxBounces_>0)に達した後は、壁/床に触れても反射せずそのまま着弾させる
		// (リコシェットライフルの「反射は3回まで」用。上限が無い(0)グレラン・投げ捨て武器は
		// bounceExhausted_ が常にfalseのまま=今までどおり無制限に反射し続ける)。
		const bool bounceExhausted = (maxBounces_ > 0 && bounceCount_ >= maxBounces_);

		if (mv.hitWall) {
			if (bounceExhausted) {
				dead_ = true;
				diedOnTerrain_ = true;
				return;
			}
			velocityX_ = -velocityX_ * wallRestitution_;
			++bounceCount_;
		}
		if (mv.hitCeiling && velocityY_ > 0.0f) {
			velocityY_ = 0.0f; // 天井バウンドは狙わないシンプルな割り切り(押し返すだけ)
		}
		if (mv.grounded) {
			if (floorRestitution_ > 0.0f && !bounceExhausted) {
				velocityY_ = -velocityY_ * floorRestitution_; // 床でも跳ね続ける(グレラン/リコシェットライフル)
				++bounceCount_;
			} else {
				velocityY_ = 0.0f;
				dead_ = true; // 着地確定=静止(投げ捨てた武器・反射回数を使い切った弾はここで死ぬ)
				diedOnTerrain_ = true;
				return;
			}
		}
		if (!stage_->IsPointInsideBounds(position_)) {
			dead_ = true;
			diedOnTerrain_ = true;
			return;
		}
	}

	if (visual_) {
		visual_->SetTranslate(position_);
		visual_->Update();
	}
	if (model_) {
		spinAngle_ += 6.0f * dt; // 投げた武器がくるくる回りながら飛ぶ
		model_->SetTranslate(position_);
		model_->SetRotate({ 0.0f, 0.0f, spinAngle_ });
		model_->Update();
	}
}

void ArcingProjectile::Draw() {
	if (visual_ && !dead_) {
		visual_->Draw();
	}
}

void ArcingProjectile::DrawModel(DirectXCore* dxCore) {
	if (model_ && !dead_) {
		model_->Draw(dxCore);
	}
}

void ArcingProjectile::TryHitCharacter(Character& defender) {
	if (dead_ || &defender == owner_) {
		return; // 既に消滅済み、または発射者自身には当てない
	}

	// 距離ダメージ減衰(Shotgun等)。knockbackPowerは対象外(ProjectileSpawnRequest::damageFalloffRange
	// のコメント参照)。
	float damage = damage_;
	if (damageFalloffRange_ > 0.0f) {
		const float t = std::clamp(GetTraveledDistance() / damageFalloffRange_, 0.0f, 1.0f);
		damage *= 1.0f + (minDamageMultiplier_ - 1.0f) * t; // lerp(1.0, minDamageMultiplier_, t)
	}

	Character::AttackHitbox hitbox;
	hitbox.center = position_;
	hitbox.radius = radius_;
	hitbox.damage = damage;
	hitbox.knockbackPower = knockbackPower_;
	// ノックバックは「飛んでいる方向」をそのまま使う(Character::AttackHitbox.knockbackDirX と
	// 同じ考え方: 命中位置から逆算すると密着距離で符号が反転するバグになるため)。
	hitbox.knockbackDirX = (velocityX_ >= 0.0f) ? 1.0f : -1.0f;

	if (defender.ReceiveHit(hitbox)) {
		dead_ = true; // 命中したので消える(貫通はしない)
	}
}

bool ArcingProjectile::TryProximityDetonate(const Character& target) {
	if (dead_ || proximityRadius_ <= 0.0f || &target == owner_) {
		return false; // 既に消滅済み/センサー無効/発射者自身はセンサー対象外(「敵が入ったら」の仕様)
	}
	const float dx = target.GetPosition().x - position_.x;
	const float dy = target.GetPosition().y - position_.y;
	if (std::sqrt(dx * dx + dy * dy) > proximityRadius_) {
		return false;
	}
	dead_ = true; // ここでは直撃扱いにしない(diedOnTerrain_は立てない)。爆風はblastRadius_>0を見て
	              // GameScene::UpdateFlyingObjects 側が「死んだから」起爆するパイプラインに乗る。
	return true;
}

float ArcingProjectile::GetTraveledDistance() const {
	const float dx = position_.x - origin_.x;
	const float dy = position_.y - origin_.y;
	return std::sqrt(dx * dx + dy * dy);
}

void ArcingProjectile::SetThrownWeaponPayload(std::unique_ptr<Weapon> weapon) {
	thrownWeaponPayload_ = std::move(weapon);
}

std::unique_ptr<Weapon> ArcingProjectile::TakeThrownWeaponPayload() {
	return std::move(thrownWeaponPayload_);
}
