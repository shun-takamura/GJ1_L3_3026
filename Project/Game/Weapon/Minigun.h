#pragma once

#include <cstdint>

#include "Weapon.h"

/// <summary>
/// 最大連射・最低威力の重機関銃。AssaultRifleと同じ連射パターン(押しっぱなしの間、
/// クールダウンが切れるたびに自動発射)だが、連射速度をさらに上げ1発の威力をさらに下げた
/// 極端側の武器。反動は連射武器の設計方針どおり小さいまま据え置く(AssaultRifle.h 参照)。
/// </summary>
class Minigun : public Weapon {
public:
	~Minigun() override;

	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "Minigun"; }
	int GetRemainingAmmo() const override { return ammo_; }
	std::string GetModelDirectory() const override { return "Resources/Models/Minigun"; }
	std::string GetModelFileName() const override { return "Minigun.mesh"; }

	/// <summary>「Weapon Tuning」ImGuiウィンドウから呼ばれるバランス調整スライダー群(Pistol.h 参照)。</summary>
	static void DrawImGuiTuning();

private:
	// static inline(constexprではない)。ImGuiで実行時に調整できるようにするため(Pistol.h の設計コメント参照)。
	static inline int   kStartingAmmo = 60;   // 全銃中最多(連射で瞬時に消費するため)
	static inline float kCooldown = 0.05f;    // 全銃中最速(1秒に約20発。AssaultRifleの倍)
	static inline float kMuzzleSpeed = 18.0f;
	static inline float kDamage = 3.0f;       // 全銃中最低(AssaultRifleの6よりさらに低い)
	static inline float kKnockbackPower = 1.5f;
	// 連射武器なので反動は小さいまま据え置く(AssaultRifle.h の設計コメント参照:
	// Character::ApplyKnockback が上書き式のため、連射中に反動を大きくしても
	// 「同じ値へ毎ショットリセットされ続ける」だけで気持ちよくならない)。
	static inline float kRecoilPower = 1.0f;
	static inline float kGravityScale = 0.8f;
	static inline float kRadius = 0.13f;
	static inline float kLifeTime = 2.5f;
	static constexpr float kMuzzleForwardOffset = 1.0f;

	// 発射音(SE/Minigun/Minigun_Fire.mp3)の実際の長さ(秒)。AssaultRifle.h と同じ理由で、
	// SoundManager に再生完了を問い合わせる手段が無いため自前のタイマーでループさせる。
	static constexpr float kFireSoundDuration = 1.87f;

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;

	// ---- 発射音ループの状態 ----
	uint32_t fireSoundHandle_ = 0;   // 0=再生していない(SoundManager::Play3DSound の仕様と対応)
	float fireSoundTimer_ = 0.0f;    // 0以下になったらクリップを鳴らし直す
};
