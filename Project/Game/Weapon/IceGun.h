#pragma once

#include "Weapon.h"

/// <summary>
/// 単発式の氷銃(B: 状態異常 ── ダメージそのものではなく相手の移動速度を奪うことで攻めどころを
/// 作る武器)。TryRangedAttack の構造自体は Pistol と同じ単発トリガーパターンだが、
/// spawn.slowMultiplier/slowDuration を追加で埋め、命中した相手を一定時間 kSlowMultiplier 倍の
/// 遅さにする(Character::ApplySlow/AttackHitbox::slowDuration 参照)。
///
/// ダメージ・ノックバックはあえて低め: この武器の価値は「1発で削る/吹き飛ばす」ことではなく
/// 「相手の逃げ足を奪って他の攻めを通しやすくする」ことにあるため、他の単発武器と同じ水準の
/// 直撃力まで持たせると強すぎる(状態異常が実質おまけになってしまう)。
/// </summary>
class IceGun : public Weapon {
public:
	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "IceGun"; }
	int GetRemainingAmmo() const override { return ammo_; }
	std::string GetModelDirectory() const override { return "Resources/Models/IceGun"; }
	std::string GetModelFileName() const override { return "IceGun.mesh"; }

	/// <summary>「Weapon Tuning」ImGuiウィンドウから呼ばれるバランス調整スライダー群(Pistol.h 参照)。</summary>
	static void DrawImGuiTuning();

private:
	// static inline(constexprではない)。ImGuiで実行時に調整できるようにするため(Pistol.h の設計コメント参照)。
	static inline int   kStartingAmmo = 5;
	static inline float kCooldown = 0.5f;
	static inline float kMuzzleSpeed = 15.0f;
	static inline float kDamage = 6.0f;        // Pistol(10)より低め。価値は減速そのもの
	static inline float kKnockbackPower = 3.0f;
	static inline float kRecoilPower = 3.5f;   // 単発武器なので毎発しっかり感じられる大きさ(Pistol.h参照)
	static inline float kGravityScale = 1.0f;
	static inline float kRadius = 0.15f;
	static inline float kLifeTime = 3.0f;
	static constexpr float kMuzzleForwardOffset = 1.0f;

	// ---- 減速(状態異常) ----
	static inline float kSlowMultiplier = 0.35f; // 命中後、通常の35%の速さになる
	static inline float kSlowDuration = 2.5f;    // この秒数だけ持続する

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
