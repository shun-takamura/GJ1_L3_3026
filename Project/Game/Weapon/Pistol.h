#pragma once

#include "Weapon.h"

/// <summary>
/// 単発式の拳銃。反動・威力・弾速のすべてが3種の中で最も小さい、扱いやすい武器。
/// トリガーの立ち上がりでのみ発射する(押しっぱなしでは連射しない)。
/// </summary>
class Pistol : public Weapon {
public:
	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "Pistol"; }
	int GetRemainingAmmo() const override { return ammo_; }

private:
	static constexpr int   kStartingAmmo = 12;
	static constexpr float kCooldown = 0.35f;    // 単発なので連射武器より長め
	static constexpr float kMuzzleSpeed = 14.0f; // 3種の中で最も遅い(=山なりが目立つ)
	static constexpr float kDamage = 10.0f;
	static constexpr float kKnockbackPower = 6.0f;
	static constexpr float kRecoilPower = 2.0f;  // 3種の中で最も小さい反動
	static constexpr float kGravityScale = 1.0f;
	static constexpr float kRadius = 0.15f;
	static constexpr float kLifeTime = 3.0f;
	static constexpr float kMuzzleForwardOffset = 1.0f; // 発射位置を自分の中心からどれだけ照準方向へ離すか

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
