#pragma once

#include "Weapon.h"

/// <summary>
/// 素手(無武装)。
///
/// 「投げ捨てられない Weapon」として実装することで、Character 側に
/// 「今、武器を持っているかどうか」の特別な分岐(nullptrチェック等)を作らずに済む
/// (Character::equippedWeapon_ は常にこの UnarmedWeapon か、いずれかの銃を指している)。
///
/// フェーズ1で Character 自身が持っていた素手パンチのロジック(判定球の位置・
/// ダメージ・ノックバック・クールダウン)をそのままここへ移設したもの。
/// </summary>
class UnarmedWeapon : public Weapon {
public:
	bool TryMeleeAttack(float dt, bool triggered, const Vector3& ownerPos,
		float aimDirX, float aimDirY, Character::AttackHitbox& outHitbox) override;

	/// <summary>素手は投げ捨てられない(投げ捨てても手ぶらのままになってしまうため)。</summary>
	bool CanBeThrown() const override { return false; }

	std::string GetName() const override { return "Unarmed"; }

private:
	static constexpr float kAttackForwardOffset = 1.0f;   // 攻撃判定球を、自分の位置から照準方向へどれだけ離すか
	static constexpr float kAttackRadius = 0.8f;          // 攻撃判定球の半径
	static constexpr float kAttackDamage = 15.0f;         // 1発分のダメージ
	static constexpr float kAttackKnockbackPower = 10.0f; // 命中時のノックバック初速
	static constexpr float kAttackCooldown = 0.5f;        // 攻撃後、次の攻撃が出せるようになるまでの秒数

	float cooldownTimer_ = 0.0f; // 0以下になるまで次の攻撃を出せない
};
