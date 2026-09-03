#pragma once

#include "Weapon.h"

/// <summary>
/// 散弾銃。1回のトリガーで複数弾(ペレット)を扇状に同時発射する。
/// 短射程(寿命が短く、すぐ消える)・高威力・大きめの反動を持つ。
/// </summary>
class Shotgun : public Weapon {
public:
	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "Shotgun"; }
	int GetRemainingAmmo() const override { return ammo_; }
	std::string GetModelDirectory() const override { return "Resources/Models/Shotgun"; }
	std::string GetModelFileName() const override { return "Shotgun.mesh"; }

private:
	static constexpr int   kStartingAmmo = 6;
	static constexpr int   kPelletCount = 6;         // 1トリガーで飛び出すペレット数
	static constexpr float kSpreadAngleRad = 0.35f;  // 散弾の広がり角(照準方向を中心に±この角度)
	static constexpr float kCooldown = 0.8f;         // 単発威力が高い分、次弾までの間隔も長い
	static constexpr float kMuzzleSpeed = 16.0f;
	static constexpr float kDamagePerPellet = 5.0f;  // 至近距離で全弾命中すると素手より遥かに高威力
	static constexpr float kKnockbackPower = 8.0f;
	static constexpr float kRecoilPower = 6.0f;      // 3種の中で最も大きい反動
	static constexpr float kGravityScale = 1.2f;
	static constexpr float kRadius = 0.12f;
	static constexpr float kLifeTime = 0.6f;         // 短射程(すぐ寿命切れで消える)
	static constexpr float kMuzzleForwardOffset = 1.0f;

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
