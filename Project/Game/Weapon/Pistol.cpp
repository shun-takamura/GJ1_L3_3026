#include "Pistol.h"

/// <summary>
/// クールダウン・残弾を見て、撃てるなら弾1発ぶんの ProjectileSpawnRequest を組み立てて
/// outSpawns に積む。実際に弾(ArcingProjectile)を生成するのは GameScene 側の仕事で、
/// ここでは「どんな初期条件の弾を1発飛ばしたいか」というリクエストを作るだけ。
/// </summary>
bool Pistol::TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
	float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) {
	(void)held; // ピストルは単発。トリガーの立ち上がり(triggered)でのみ発射する(held は使わない)

	// クールダウン中は毎フレーム減らしていくだけ(Character の attackCooldownTimer_ と同じ考え方)。
	if (cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= dt;
	}
	// トリガーを引いていない/クールダウン中/弾切れ、のいずれかなら発射不成立。
	if (!triggered || cooldownTimer_ > 0.0f || ammo_ <= 0) {
		return false; // 弾切れでも武器はそのまま(自動では捨てない)。撃てないだけ
	}
	cooldownTimer_ = kCooldown;
	--ammo_;

	ProjectileSpawnRequest spawn;
	// 発射位置は自分の中心から照準方向へ少し離す(自分の当たり判定に自分の弾が
	// めり込んだ状態で生まれて誤判定する事故を避けるため。銃口の位置のイメージ)。
	spawn.origin = {
		ownerPos.x + aimDirX * kMuzzleForwardOffset,
		ownerPos.y + aimDirY * kMuzzleForwardOffset,
		ownerPos.z
	};
	// 初速は照準方向 × 弾速。この後は ArcingProjectile 側が重力を積分して放物線を描く。
	spawn.velocityX = aimDirX * kMuzzleSpeed;
	spawn.velocityY = aimDirY * kMuzzleSpeed;
	spawn.gravityScale = kGravityScale; // 重力の掛かり具合(1.0でCharacterと同じ重力加速度)
	spawn.radius = kRadius;             // 弾の当たり判定半径
	spawn.lifeTime = kLifeTime;         // 何にも当たらなくてもこの秒数で消える
	spawn.damage = kDamage;             // 命中時のダメージ
	spawn.knockbackPower = kKnockbackPower; // 命中時のノックバックの大きさ
	outSpawns.push_back(spawn);
	return true;
}
