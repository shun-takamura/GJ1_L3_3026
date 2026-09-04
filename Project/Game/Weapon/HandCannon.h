#pragma once

#include "Weapon.h"

/// <summary>
/// 弾数1〜2発の緊急/フィニッシャー武器。Pistolと同じ単発パターンだが、
/// 持ち込み弾数を極限まで削るかわりに1発の威力・ノックバックを全銃中トップクラスに振ってある。
/// 「削り切れなかった相手にとどめを刺す」「場外まで吹き飛ばす」用途を想定。
/// </summary>
class HandCannon : public Weapon {
public:
	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "HandCannon"; }
	int GetRemainingAmmo() const override { return ammo_; }
	std::string GetModelDirectory() const override { return "Resources/Models/HandCannon"; }
	std::string GetModelFileName() const override { return "HandCannon.mesh"; }

	/// <summary>「Weapon Tuning」ImGuiウィンドウから呼ばれるバランス調整スライダー群(Pistol.h 参照)。</summary>
	static void DrawImGuiTuning();

private:
	// static inline(constexprではない)。ImGuiで実行時に調整できるようにするため(Pistol.h の設計コメント参照)。
	static inline int   kStartingAmmo = 2;    // 全銃中最少(緊急用途なので使い切り前提)
	static inline float kCooldown = 0.6f;     // 弾数が少ないのでクールダウン自体はさほど重要ではない
	static inline float kMuzzleSpeed = 16.0f;
	static inline float kDamage = 35.0f;      // SniperRifle(60)には劣るが直撃武器の中では高威力
	// 全銃中最大のノックバック。フィニッシャーとして「削り切れなかった相手を場外へ送る」役割を
	// 数値そのもので担わせている(ダメージだけでなく吹き飛ばし力そのものが売り)。
	static inline float kKnockbackPower = 20.0f;
	// 反動公式(移動距離 ≈ power/6.0)で約2.5ユニット。SniperRifle(18.0)に次ぐ大きさ。
	static inline float kRecoilPower = 15.0f;
	static inline float kGravityScale = 1.0f;
	static inline float kRadius = 0.2f;       // 弾数が少ない分、判定はやや甘めにして外れ感を減らす
	static inline float kLifeTime = 2.0f;     // 近距離での使用を想定した短射程
	static constexpr float kMuzzleForwardOffset = 1.0f;

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
