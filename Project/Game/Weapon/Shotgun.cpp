#include "Shotgun.h"

#include <cmath>

/// <summary>
/// クールダウン・残弾を見て、撃てるなら kPelletCount 発ぶんの ProjectileSpawnRequest
/// (扇状に散らしたペレット)を一度に outSpawns へ積む。弾数消費(--ammo_)は
/// 「1トリガーにつき1」であり、ペレットの本数ではない点に注意。
/// </summary>
bool Shotgun::TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
	float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) {
	(void)held; // ショットガンも単発。トリガーの立ち上がりでのみ発射する

	if (cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= dt;
	}
	if (!triggered || cooldownTimer_ > 0.0f || ammo_ <= 0) {
		return false;
	}
	cooldownTimer_ = kCooldown;
	--ammo_;

	// 照準方向を角度(ラジアン)に変換し、その前後 kSpreadAngleRad の範囲へ
	// kPelletCount 発を均等に振り分ける(Vector3 に回転を表す演算子が無いため、
	// atan2/cos/sin を使った角度計算で代用している)。
	const Vector3 muzzlePos = {
		ownerPos.x + aimDirX * kMuzzleForwardOffset,
		ownerPos.y + aimDirY * kMuzzleForwardOffset,
		ownerPos.z
	};
	const float baseAngle = std::atan2(aimDirY, aimDirX);
	for (int i = 0; i < kPelletCount; ++i) {
		// t は -1.0(一番左のペレット)〜 +1.0(一番右のペレット)を均等割りした値。
		const float t = (kPelletCount <= 1)
			? 0.0f
			: (static_cast<float>(i) / static_cast<float>(kPelletCount - 1)) * 2.0f - 1.0f;
		const float angle = baseAngle + t * kSpreadAngleRad;

		ProjectileSpawnRequest spawn;
		spawn.origin = muzzlePos; // 全ペレット同じ発射位置(向きだけ扇状にずらす)
		spawn.velocityX = std::cos(angle) * kMuzzleSpeed;
		spawn.velocityY = std::sin(angle) * kMuzzleSpeed;
		spawn.gravityScale = kGravityScale; // 3種の中で最も重力が強く、寿命も短いので弾道は短く落ちる
		spawn.radius = kRadius;
		spawn.lifeTime = kLifeTime;         // 短射程(すぐ寿命切れで消える)
		spawn.damage = kDamagePerPellet;    // 1ペレットぶんのダメージ(全弾命中なら合計は高威力)
		spawn.knockbackPower = kKnockbackPower;
		outSpawns.push_back(spawn);
	}
	return true;
}
