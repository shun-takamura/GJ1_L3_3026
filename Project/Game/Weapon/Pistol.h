#pragma once

#include "Weapon.h"

/// <summary>
/// 単発式の拳銃。威力・弾速は控えめで扱いやすいが、反動(kRecoilPower)は連射武器の
/// AssaultRifle よりは大きめに振ってある ── 「撃った実感」を単発武器では毎回感じられるように
/// する方針(連射武器だけ反動を小さく抑える設計。AssaultRifle.h の設計コメント参照)。
/// トリガーの立ち上がりでのみ発射する(押しっぱなしでは連射しない)。
/// </summary>
class Pistol : public Weapon {
public:
	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "Pistol"; }
	int GetRemainingAmmo() const override { return ammo_; }
	std::string GetModelDirectory() const override { return "Resources/Models/Pistol"; }
	std::string GetModelFileName() const override { return "Pistol.mesh"; }

	/// <summary>「Weapon Tuning」ImGuiウィンドウ(GameScene::Initialize で登録)から呼ばれる、
	/// このクラス自身のバランス調整スライダー群。GetName() 等と同じく static なので、
	/// インスタンスを1つも持っていなくても(まだ誰も装備/所持していなくても)常に呼べる。</summary>
	static void DrawImGuiTuning();

private:
	// static inline(constexprではない)にしてあるのは、ImGuiのスライダーで実行時に
	// 直接書き換えられるようにするため。同じ型の全インスタンスがこの値を共有するので、
	// ここを動かすと「今持っている銃」だけでなく「これから拾う銃」にも反映される
	// (逆に言うと、値を戻すにはアプリを再起動する必要がある。恒久的な調整値は
	// このファイルのデフォルト値自体を書き換えること)。
	static inline int   kStartingAmmo = 12;
	static inline float kCooldown = 0.35f;    // 単発なので連射武器より長め
	static inline float kMuzzleSpeed = 14.0f; // 3種の中で最も遅い(=山なりが目立つ)
	static inline float kDamage = 10.0f;
	static inline float kKnockbackPower = 6.0f;
	static inline float kRecoilPower = 3.5f;  // 単発武器なので毎発しっかり感じられる大きさにしてある
	static inline float kGravityScale = 1.0f;
	static inline float kRadius = 0.15f;
	static inline float kLifeTime = 3.0f;
	static constexpr float kMuzzleForwardOffset = 1.0f; // 発射位置を自分の中心からどれだけ照準方向へ離すか(調整頻度が低いので定数のまま)

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
