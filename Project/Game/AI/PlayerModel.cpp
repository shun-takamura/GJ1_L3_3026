#include "AI/PlayerModel.h"

#include <algorithm>
#include <cmath>

#include "Character/Character.h"
#include "Log.h"

namespace {
	float Lerp(float a, float b, float t) {
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return a + (b - a) * t;
	}
}

void PlayerModel::Reset() {
	tier_ = 0;
	observedTime_ = 0.0f;
	rangeSum_ = 0.0f;
	rangeSamples_ = 0;
	campingBreakableFloor_ = false;
}

void PlayerModel::Observe(const Character& player, const Character& enemy, float dt) {
	if (dt <= 0.0f) {
		return;
	}
	observedTime_ += dt;

	// 計測できるものだけ: プレイヤーが AI との間にどれくらいの距離を保ちがちか。
	const Vector3 p = player.GetPosition();
	const Vector3 e = enemy.GetPosition();
	const float dx = p.x - e.x;
	const float dy = p.y - e.y;
	rangeSum_ += std::sqrt(dx * dx + dy * dy);
	++rangeSamples_;

	// TODO(Day5): ジャンプ頻度・しゃがみ割合・壊れ床居座りの計測。
	//   Character にしゃがみ/接地の getter を足すか、GameScene 側から
	//   「弾をしゃがんで避けた」「壊れ床の上にいる」をイベントで渡す。
}

void PlayerModel::OnPointConceded() {
	tier_ = (std::min)(tier_ + 1, kMaxTier);
	Log("PlayerModel: プレイヤーに得点された -> 学習ティア " + std::to_string(tier_) + "\n");
}

float PlayerModel::ReactionDelay() const {
	const float t = static_cast<float>(tier_) / static_cast<float>(kMaxTier);
	return Lerp(kReactionAtTier0, kReactionAtMaxTier, t);
}

float PlayerModel::AimErrorRad() const {
	const float t = static_cast<float>(tier_) / static_cast<float>(kMaxTier);
	return Lerp(kAimErrAtTier0, kAimErrAtMaxTier, t);
}

float PlayerModel::PreferredRange() const {
	if (rangeSamples_ <= 0) {
		return 6.0f; // まだ計測できていない間の無難な既定値
	}
	return rangeSum_ / static_cast<float>(rangeSamples_);
}

float PlayerModel::CrouchRatio() const {
	return 0.0f; // TODO(Day5)
}

float PlayerModel::JumpsPerSecond() const {
	return 0.0f; // TODO(Day5)
}
