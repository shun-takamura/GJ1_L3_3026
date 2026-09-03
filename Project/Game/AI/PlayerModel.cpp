#include "AI/PlayerModel.h"

#include <algorithm>
#include <cmath>

#include "Character/Character.h"
#include "Common/IStageQuery.h"
#include "Log.h"

namespace {
	float Lerp(float a, float b, float t) {
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return a + (b - a) * t;
	}
}

float PlayerModel::RampT() const {
	float t = static_cast<float>(tier_) / static_cast<float>(kMaxTier);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	// 二次(既定)カーブ: 序盤はほぼ tier0 のまま、終盤で一気に最大へ寄る。
	return std::pow(t, kRampExponent);
}

void PlayerModel::Reset() {
	tier_ = 0;
	observedTime_ = 0.0f;
	rangeSum_ = 0.0f;
	rangeSamples_ = 0;
	jumpCount_ = 0;
	prevGroundedValid_ = false;
	prevGrounded_ = true;
	prevPlayerY_ = 0.0f;
	crouchTime_ = 0.0f;
	campingTime_ = 0.0f;
}

void PlayerModel::Observe(const Character& player, const Character& enemy,
	const IStageQuery* stage, float dt) {
	if (dt <= 0.0f) {
		return;
	}
	observedTime_ += dt;

	const Vector3 p = player.GetPosition();
	const Vector3 e = enemy.GetPosition();

	// --- 好む間合い ---
	const float dx = p.x - e.x;
	const float dy = p.y - e.y;
	rangeSum_ += std::sqrt(dx * dx + dy * dy);
	++rangeSamples_;

	// --- ジャンプ頻度 ---
	// 接地 → 空中 に変わった瞬間で、かつ実際に上へ動いていれば「ジャンプ」とみなす
	// （崖から歩いて落ちただけのときは Y が下がるので除外する）。
	const bool grounded = player.IsGrounded();
	if (prevGroundedValid_) {
		const bool leftGround = prevGrounded_ && !grounded;
		const bool rising = (p.y - prevPlayerY_) > 0.01f;
		if (leftGround && rising) {
			++jumpCount_;
		}
	}
	prevGrounded_ = grounded;
	prevPlayerY_ = p.y;
	prevGroundedValid_ = true;

	// --- しゃがみ割合 ---
	if (player.IsCrouching()) {
		crouchTime_ += dt;
	}

	// --- 危うい壊れ床への居座り ---
	if (stage && stage->IsPrecariousBreakableFloor(p)) {
		campingTime_ += dt;
	}

	// TODO(Day5): 「弾をしゃがんで避けた」瞬間そのものは、GameScene 側で
	//   『プレイヤーの頭上を弾が通過 && しゃがみ中』をイベントとして拾うほうが精度が高い。
}

void PlayerModel::OnPointConceded() {
	tier_ = (std::min)(tier_ + 1, kMaxTier);
	Log("PlayerModel: プレイヤーに得点された -> 学習ティア " + std::to_string(tier_) + "\n");
}

float PlayerModel::ReactionDelay() const {
	return Lerp(kReactionAtTier0, kReactionAtMaxTier, RampT());
}

float PlayerModel::AimErrorRad() const {
	return Lerp(kAimErrAtTier0, kAimErrAtMaxTier, RampT());
}

float PlayerModel::PreferredRange() const {
	if (rangeSamples_ <= 0) {
		return 6.0f; // まだ計測できていない間の無難な既定値
	}
	return rangeSum_ / static_cast<float>(rangeSamples_);
}

float PlayerModel::CrouchRatio() const {
	if (observedTime_ <= 0.0f) {
		return 0.0f;
	}
	return crouchTime_ / observedTime_;
}

float PlayerModel::JumpsPerSecond() const {
	if (observedTime_ <= 0.0f) {
		return 0.0f;
	}
	return static_cast<float>(jumpCount_) / observedTime_;
}

bool PlayerModel::LikesCampingBreakable() const {
	if (observedTime_ <= 0.0f) {
		return false;
	}
	return (campingTime_ / observedTime_) > kCampingRatioThreshold;
}
