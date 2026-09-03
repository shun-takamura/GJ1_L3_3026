#pragma once

#include "Weapon.h"

/// <summary>
/// 連射式のアサルトライフル。攻撃入力を押しっぱなしにしている間、
/// クールダウンが切れるたびに自動で発射し続ける。1発あたりの威力・反動は控えめ。
/// </summary>
class AssaultRifle : public Weapon {
public:
	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "AssaultRifle"; }
	int GetRemainingAmmo() const override { return ammo_; }
	std::string GetModelDirectory() const override { return "Resources/Models/AssaultRifle"; }
	std::string GetModelFileName() const override { return "AssaultRifle.mesh"; }

private:
	static constexpr int   kStartingAmmo = 30;
	static constexpr float kCooldown = 0.1f;     // 連射(1秒に約10発)
	static constexpr float kMuzzleSpeed = 20.0f; // 3種の中で最も速い(=弾道が平ら)
	static constexpr float kDamage = 6.0f;       // 1発の威力は低いが連射で補う
	static constexpr float kKnockbackPower = 3.0f;
	static constexpr float kRecoilPower = 1.5f;
	static constexpr float kGravityScale = 0.8f; // 弾速が速い分、重力の影響をやや弱くして扱いやすくする
	static constexpr float kRadius = 0.12f;
	static constexpr float kLifeTime = 2.5f;
	static constexpr float kMuzzleForwardOffset = 1.0f;

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
