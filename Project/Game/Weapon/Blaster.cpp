#include "Blaster.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

/// <summary>
/// クールダウン・残弾を見て、撃てるなら爆風弾1発ぶんの ProjectileSpawnRequest を組み立てて
/// outSpawns に積む。他の銃との違いは blastRadius を設定している点のみ ── 実際の爆発処理
/// (距離減衰・発射者自身への巻き込み)は ArcingProjectile / GameScene::ResolveExplosion 側が
/// 共通で担うので、ここでは「爆風を持つ弾を1発リクエストする」以上のことはしない。
/// </summary>
bool Blaster::TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
	float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) {
	(void)held; // 単発武器。トリガーの立ち上がり(triggered)でのみ発射する

	if (cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= dt;
	}
	if (!triggered || cooldownTimer_ > 0.0f || ammo_ <= 0) {
		return false;
	}
	cooldownTimer_ = kCooldown;
	--ammo_;

	ProjectileSpawnRequest spawn;
	spawn.origin = {
		ownerPos.x + aimDirX * kMuzzleForwardOffset,
		ownerPos.y + aimDirY * kMuzzleForwardOffset,
		ownerPos.z
	};
	spawn.velocityX = aimDirX * kMuzzleSpeed;
	spawn.velocityY = aimDirY * kMuzzleSpeed;
	spawn.gravityScale = kGravityScale;
	spawn.radius = kRadius;
	spawn.lifeTime = kLifeTime;
	spawn.damage = kDamage;             // 爆心での最大ダメージ(実際に入る量は距離減衰後)
	spawn.knockbackPower = kKnockbackPower; // 爆心での最大ノックバック(同上)
	spawn.blastRadius = kBlastRadius;   // これが 0 より大きいことで GameScene 側が爆発扱いする
	outSpawns.push_back(spawn);
	return true;
}

void Blaster::DrawImGuiTuning() {
#ifdef USE_IMGUI
	ImGui::DragInt("Starting Ammo", &kStartingAmmo, 1.0f, 1, 20);
	ImGui::DragFloat("Cooldown (s)", &kCooldown, 0.05f, 0.1f, 5.0f);
	ImGui::DragFloat("Muzzle Speed", &kMuzzleSpeed, 0.1f, 1.0f, 40.0f);
	ImGui::DragFloat("Gravity Scale", &kGravityScale, 0.05f, 0.0f, 3.0f);
	ImGui::DragFloat("Bullet Radius", &kRadius, 0.01f, 0.01f, 1.0f);
	ImGui::DragFloat("Life Time (s)", &kLifeTime, 0.05f, 0.1f, 10.0f);
	ImGui::DragFloat("Blast Radius", &kBlastRadius, 0.05f, 0.0f, 10.0f);
	ImGui::Separator();
	ImGui::DragFloat("Damage (at center)", &kDamage, 0.5f, 0.0f, 100.0f);
	ImGui::DragFloat("Knockback Power (at center)", &kKnockbackPower, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Recoil Power", &kRecoilPower, 0.5f, 0.0f, 50.0f);
#endif
}
