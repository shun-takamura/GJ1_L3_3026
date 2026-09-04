#pragma once

#include "Weapon.h"

/// <summary>
/// 単発式の炎銃(B: 状態異常)。TryRangedAttack 自体は Pistol と同じ単発トリガーパターンだが、
/// 直撃した相手を燃やす(burnDps/burnDuration)のに加えて、地形に着弾/寿命切れで消えた場合に
/// 限り、その場に炎(FireHazard)を残す(spawnsFireHazard 以下。ProjectileSpawnRequest.h の
/// 設計コメント参照)。キャラクターに直撃した場合は炎を残さない ── 直撃した相手は既に
/// burnDps/burnDuration で燃えているので、そこへさらに地面の炎(範囲攻撃)まで広げると
/// 1発で直撃燃焼+範囲燃焼の二重取りになってしまうため
/// (GameScene::UpdateFlyingObjects の diedFromCharacterHit 判定を参照)。
///
/// 直撃ダメージ自体は控えめにしてある。この武器の強さは「1発の直撃」ではなく
/// 「命中後も燃え続ける」「着弾点そのものが(狭いが)立ち入れない場所になる」という
/// 時間経過込みの削りにあるため。着弾点の炎は発射者自身が踏んでも同じように燃える ──
/// Blaster の「自分の爆風にも巻き込まれる」と同じ、位置関係そのものをリスクにする設計を
/// 踏襲している(GameScene::SpawnFireHazard / FireHazard.h 参照)。
/// </summary>
class FireGun : public Weapon {
public:
	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "FireGun"; }
	int GetRemainingAmmo() const override { return ammo_; }
	std::string GetModelDirectory() const override { return "Resources/Models/FireGun"; }
	std::string GetModelFileName() const override { return "FireGun.mesh"; }

	/// <summary>「Weapon Tuning」ImGuiウィンドウから呼ばれるバランス調整スライダー群(Pistol.h 参照)。</summary>
	static void DrawImGuiTuning();

private:
	// static inline(constexprではない)。ImGuiで実行時に調整できるようにするため(Pistol.h の設計コメント参照)。
	static inline int   kStartingAmmo = 4;
	static inline float kCooldown = 0.6f;
	static inline float kMuzzleSpeed = 14.0f;
	static inline float kDamage = 8.0f;        // 直撃の即時ダメージは控えめ(燃焼と地面の炎で補う)
	static inline float kKnockbackPower = 4.0f;
	static inline float kRecoilPower = 4.0f;
	static inline float kGravityScale = 1.0f;
	static inline float kRadius = 0.16f;
	static inline float kLifeTime = 3.0f;
	static constexpr float kMuzzleForwardOffset = 1.0f;

	// ---- 燃焼(直撃した相手への継続ダメージ) ----
	static inline float kBurnDps = 6.0f;
	static inline float kBurnDuration = 3.0f;

	// ---- 着弾点を燃やす(FireHazard) ----
	static inline float kFireHazardRadius = 0.9f; // 以前は1.5f。範囲が広すぎたため縮小
	static inline float kFireHazardDuration = 4.0f;
	static inline float kFireHazardDps = 8.0f; // 直撃burnDpsより高め(踏み続けるリスクを明確にする)

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
