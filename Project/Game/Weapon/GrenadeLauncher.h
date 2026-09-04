#pragma once

#include "Weapon.h"

/// <summary>
/// 山なりに飛んで壁・床を跳ね続ける爆風武器。Blasterと違い着弾で即爆発せず、
/// 3つの起爆条件のどれかが成立するまで跳ね回る:
///   1. キャラクターに直撃する(ArcingProjectile::TryHitCharacter)
///   2. 発射者以外のキャラクターが爆風範囲(kProximityRadius)に入る
///      (ArcingProjectile::TryProximityDetonate。「近くに敵が来たら勝手に爆発する」近接センサー)
///   3. kLifeTime(フューズ)が尽きる(既存の寿命切れの仕組みをそのまま流用)
/// どの起爆でも、実際の爆風適用(発射者自身を含む距離減衰ダメージ/ノックバック・地形破壊)は
/// blastRadius>0 を見た GameScene::ResolveExplosion 側が共通で担う(Blasterと同じパイプライン)。
///
/// バウンド回数の上限は設けない(ProjectileSpawnRequest::floorRestitution を0より大きくして
/// 床でも跳ね続けさせている。壁はkWallRestitution)。
/// </summary>
class GrenadeLauncher : public Weapon {
public:
	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "GrenadeLauncher"; }
	int GetRemainingAmmo() const override { return ammo_; }
	std::string GetModelDirectory() const override { return "Resources/Models/GrenadeLauncher"; }
	std::string GetModelFileName() const override { return "GrenadeLauncher.mesh"; }

	/// <summary>「Weapon Tuning」ImGuiウィンドウから呼ばれるバランス調整スライダー群(Pistol.h 参照)。</summary>
	static void DrawImGuiTuning();

private:
	// static inline(constexprではない)。ImGuiで実行時に調整できるようにするため(Pistol.h の設計コメント参照)。
	static inline int   kStartingAmmo = 2;     // 銃系の中でBlaster(3)よりさらに少ない(面制圧力が高いため)
	static inline float kCooldown = 1.5f;
	static inline float kMuzzleSpeed = 12.0f;
	static inline float kGravityScale = 1.0f;
	static inline float kDamage = 24.0f;         // 直撃/爆心での最大ダメージ(Blasterの26よりやや低い)
	static inline float kKnockbackPower = 18.0f; // 爆心での最大ノックバック
	// 反動公式(移動距離 ≈ power/6.0)で約2.33ユニット吹き飛ぶ。Blaster(16.0→約2.7)よりやや控えめ。
	static inline float kRecoilPower = 14.0f;
	static inline float kRadius = 0.25f;
	static inline float kLifeTime = 3.0f;        // フューズ。これが尽きたら跳ね続けていても強制起爆
	static inline float kBlastRadius = 3.0f;     // Blaster(2.5)より一回り広い。近接センサーと同じ半径を使う
	static inline float kWallRestitution = 0.65f;
	static inline float kFloorRestitution = 0.55f;
	static constexpr float kMuzzleForwardOffset = 1.0f;

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
