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

	/// <summary>「Weapon Tuning」ImGuiウィンドウから呼ばれるバランス調整スライダー群(Pistol.h 参照)。</summary>
	static void DrawImGuiTuning();

private:
	// static inline(constexprではない)。ImGuiで実行時に調整できるようにするため(Pistol.h の設計コメント参照)。
	static inline int   kStartingAmmo = 30;
	static inline float kCooldown = 0.1f;     // 連射(1秒に約10発)
	static inline float kMuzzleSpeed = 20.0f; // 3種の中で最も速い(=弾道が平ら)
	static inline float kDamage = 6.0f;       // 1発の威力は低いが連射で補う
	static inline float kKnockbackPower = 3.0f;
	// 連射武器だけは反動を意図的に小さいまま据え置いている。Character::ApplyKnockback は
	// 上書き式(前回のノックバック速度を積み増さない)なので、連射中に反動を大きくしても
	// 「同じ小さい値に毎ショットリセットされ続ける」だけで気持ちよくならず、ただ操作性が
	// 悪化するだけになる。このゲームでの反動リスクは単発・重量級武器(Pistol/Shotgun/Blaster等)
	// が担う設計方針(Pistol.h の設計コメント参照)。
	static inline float kRecoilPower = 1.5f;
	static inline float kGravityScale = 0.8f; // 弾速が速い分、重力の影響をやや弱くして扱いやすくする
	static inline float kRadius = 0.12f;
	static inline float kLifeTime = 2.5f;
	static constexpr float kMuzzleForwardOffset = 1.0f;

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
