#pragma once

#include "Weapon.h"

/// <summary>
/// 最速弾・ワンパン級ダメージ・低連射のライフル。Pistolと同じ単発パターン
/// (トリガーの立ち上がりでのみ発射)だが、全パラメータを「刺さればほぼ一撃」の
/// 極端側へ振ってある。かわりに弾数は全銃中最少クラス、クールダウンは全銃中最長。
/// </summary>
class SniperRifle : public Weapon {
public:
	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "SniperRifle"; }
	int GetRemainingAmmo() const override { return ammo_; }
	std::string GetModelDirectory() const override { return "Resources/Models/SniperRifle"; }
	std::string GetModelFileName() const override { return "SniperRifle.mesh"; }

	/// <summary>「Weapon Tuning」ImGuiウィンドウから呼ばれるバランス調整スライダー群(Pistol.h 参照)。</summary>
	static void DrawImGuiTuning();

private:
	// static inline(constexprではない)。ImGuiで実行時に調整できるようにするため(Pistol.h の設計コメント参照)。
	static inline int   kStartingAmmo = 4;    // 全銃中最少クラス(Blasterの3に次ぐ少なさ)
	static inline float kCooldown = 1.8f;     // 全銃中最長(連射不可)
	static inline float kMuzzleSpeed = 45.0f; // 全銃中最速(ほぼ直線に近い弾道)
	static inline float kDamage = 60.0f;      // 全銃中最大。HP満タン(100)から2発でほぼ即死圏内
	static inline float kKnockbackPower = 14.0f;
	// 反動公式(移動距離 ≈ power/6.0)で約3.0ユニット。Blaster(16.0)より大きい、全銃中最大の反動
	// (単発・重量級武器は相手ノックバックと同等以上まで引き上げる、という設計方針に沿う)。
	static inline float kRecoilPower = 18.0f;
	static inline float kGravityScale = 0.3f; // 弾速と合わせて、ほぼ山なりを感じさせない直線弾道にする
	static inline float kRadius = 0.12f;      // 判定は小さめ(狙って当てる武器なので甘くしない)
	static inline float kLifeTime = 2.5f;
	static constexpr float kMuzzleForwardOffset = 1.0f;

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
