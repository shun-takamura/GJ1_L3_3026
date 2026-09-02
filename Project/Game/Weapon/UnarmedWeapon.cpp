#include "UnarmedWeapon.h"

bool UnarmedWeapon::TryMeleeAttack(float dt, bool triggered, const Vector3& ownerPos,
	float aimDirX, float aimDirY, Character::AttackHitbox& outHitbox) {
	if (cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= dt;
	}
	if (!triggered || cooldownTimer_ > 0.0f) {
		return false;
	}
	cooldownTimer_ = kAttackCooldown;

	// 攻撃判定球は、自分の位置から照準方向へ kAttackForwardOffset だけ離した位置に置く
	// (＝狙っている方向を殴るイメージ。フェーズ1では左右のみだったが、マウス照準の
	// 導入で上下にもパンチを出せるようになった)。
	outHitbox.center = {
		ownerPos.x + aimDirX * kAttackForwardOffset,
		ownerPos.y + aimDirY * kAttackForwardOffset,
		ownerPos.z
	};
	outHitbox.radius = kAttackRadius;
	outHitbox.damage = kAttackDamage;
	outHitbox.knockbackPower = kAttackKnockbackPower;
	// ノックバックは水平方向のみ(Character::ApplyKnockback の仕様)なので、
	// 照準方向のX成分の符号だけを使う。
	outHitbox.knockbackDirX = (aimDirX >= 0.0f) ? 1.0f : -1.0f;
	return true;
}
