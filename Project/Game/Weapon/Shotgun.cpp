#include "Shotgun.h"

#include <cmath>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

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
		spawn.damageFalloffRange = kDamageFalloffRange; // 至近距離特化: 遠距離ほどダメージが落ちる
		spawn.minDamageMultiplier = kMinDamageMultiplier;
		outSpawns.push_back(spawn);
	}
	return true;
}

void Shotgun::DrawImGuiTuning() {
#ifdef USE_IMGUI
	ImGui::DragInt("Starting Ammo", &kStartingAmmo, 1.0f, 1, 30);
	ImGui::DragInt("Pellet Count", &kPelletCount, 1.0f, 1, 20);
	ImGui::DragFloat("Spread Angle (rad)", &kSpreadAngleRad, 0.01f, 0.0f, 1.5f);
	ImGui::DragFloat("Cooldown (s)", &kCooldown, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("Muzzle Speed", &kMuzzleSpeed, 0.1f, 1.0f, 40.0f);
	ImGui::DragFloat("Gravity Scale", &kGravityScale, 0.05f, 0.0f, 3.0f);
	ImGui::DragFloat("Pellet Radius", &kRadius, 0.01f, 0.01f, 1.0f);
	ImGui::DragFloat("Life Time (s)", &kLifeTime, 0.05f, 0.1f, 10.0f);
	ImGui::Separator();
	ImGui::DragFloat("Damage / Pellet", &kDamagePerPellet, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Knockback Power", &kKnockbackPower, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Recoil Power", &kRecoilPower, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Damage Falloff Range", &kDamageFalloffRange, 0.1f, 0.0f, 20.0f);
	ImGui::DragFloat("Min Damage Multiplier", &kMinDamageMultiplier, 0.02f, 0.0f, 1.0f);
#endif
}
