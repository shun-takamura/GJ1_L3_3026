#pragma once

#include "Weapon.h"

/// <summary>
/// 壁にも床にも当たって跳ね返り続ける単発ライフル(B: 尖った武器)。
/// GrenadeLauncher/投げ捨てた武器と同じ ProjectileSpawnRequest::bounces 基盤をそのまま流用する
/// (ArcingProjectile.h の設計コメント参照)。GrenadeLauncherとの違いは:
///   - maxBounces で反射回数を3回に制限している(無限に転がり続けると絵的にも扱い的にも
///     煩雑なため。4回目に地形へ触れた瞬間、反射せずそのまま着弾して消える)。
///   - 爆風・近接センサーは持たない。直撃した瞬間にダメージが入る素直な弾。
/// 1発の威力は控えめ(Pistol程度)にして、跳ね返りで思わぬ角度から当てられる分の
/// リスクリワードを「当てにくさ・狙いにくさ」側に置いている。
/// </summary>
class RicochetRifle : public Weapon {
public:
	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "RicochetRifle"; }
	int GetRemainingAmmo() const override { return ammo_; }
	std::string GetModelDirectory() const override { return "Resources/Models/RicochetRifle"; }
	std::string GetModelFileName() const override { return "RicochetRifle.mesh"; }

	/// <summary>「Weapon Tuning」ImGuiウィンドウから呼ばれるバランス調整スライダー群(Pistol.h 参照)。</summary>
	static void DrawImGuiTuning();

private:
	// static inline(constexprではない)。ImGuiで実行時に調整できるようにするため(Pistol.h の設計コメント参照)。
	static inline int   kStartingAmmo = 6;
	static inline float kCooldown = 0.7f;      // 単発・やや長め(跳ね返りを狙って撃つ武器なので連射させない)
	static inline float kMuzzleSpeed = 18.0f;
	static inline float kDamage = 9.0f;        // Pistol(10)相当。威力ではなく当てにくさ/狙い方でリスクリワードを作る
	static inline float kKnockbackPower = 6.0f;
	static inline float kRecoilPower = 5.0f;   // 反動公式で約0.83ユニット。単発武器の中では控えめ
	// 重力を弱めにして、壁までの弾道(=跳ね返り角度)をプレイヤーが読みやすくしてある。
	static inline float kGravityScale = 0.6f;
	static inline float kRadius = 0.14f;
	static inline float kLifeTime = 4.0f;      // 跳ね返って回り込む時間を確保するため他の単発銃より長め
	static constexpr float kMuzzleForwardOffset = 1.0f;

	// ---- 跳ね返り(ProjectileSpawnRequest::bounces系) ----
	static inline float kWallRestitution = 0.85f;  // 壁でも床でもほぼ勢いを落とさず跳ね返る(この武器の売り)
	static inline float kFloorRestitution = 0.85f; // 床でも跳ねる(壁と同じ跳ね返り方)
	static inline int   kMaxBounces = 3;           // 壁・床合わせて3回反射したら、次の接触で着弾して消える

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
