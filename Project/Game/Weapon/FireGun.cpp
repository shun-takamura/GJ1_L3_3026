#include "FireGun.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

/// <summary>
/// クールダウン・残弾を見て、撃てるなら弾1発ぶんの ProjectileSpawnRequest を組み立てて
/// outSpawns に積む(Pistol.cpp と同じ単発パターン)。burnDps/burnDuration(直撃した相手を
/// 燃やす)と spawnsFireHazard 以下(着弾点に炎を残す)の両方を埋めている点が Pistol と違う ──
/// 後者は ArcingProjectile が着弾時に GameScene::SpawnFireHazard へ渡す(GameScene.cpp 参照)。
/// </summary>
bool FireGun::TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
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
	spawn.damage = kDamage;
	spawn.knockbackPower = kKnockbackPower;
	spawn.burnDps = kBurnDps;
	spawn.burnDuration = kBurnDuration;
	spawn.spawnsFireHazard = true;
	spawn.fireHazardRadius = kFireHazardRadius;
	spawn.fireHazardDuration = kFireHazardDuration;
	spawn.fireHazardDps = kFireHazardDps;
	outSpawns.push_back(spawn);
	return true;
}

void FireGun::DrawImGuiTuning() {
#ifdef USE_IMGUI
	ImGui::DragInt("Starting Ammo", &kStartingAmmo, 1.0f, 1, 20);
	ImGui::DragFloat("Cooldown (s)", &kCooldown, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("Muzzle Speed", &kMuzzleSpeed, 0.1f, 1.0f, 40.0f);
	ImGui::DragFloat("Gravity Scale", &kGravityScale, 0.05f, 0.0f, 3.0f);
	ImGui::DragFloat("Bullet Radius", &kRadius, 0.01f, 0.01f, 1.0f);
	ImGui::DragFloat("Life Time (s)", &kLifeTime, 0.05f, 0.1f, 10.0f);
	ImGui::Separator();
	ImGui::DragFloat("Damage", &kDamage, 0.5f, 0.0f, 100.0f);
	ImGui::DragFloat("Knockback Power", &kKnockbackPower, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Recoil Power", &kRecoilPower, 0.5f, 0.0f, 50.0f);
	ImGui::Separator();
	ImGui::DragFloat("Burn DPS", &kBurnDps, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Burn Duration (s)", &kBurnDuration, 0.1f, 0.1f, 10.0f);
	ImGui::Separator();
	ImGui::DragFloat("Fire Hazard Radius", &kFireHazardRadius, 0.1f, 0.2f, 5.0f);
	ImGui::DragFloat("Fire Hazard Duration (s)", &kFireHazardDuration, 0.1f, 0.5f, 15.0f);
	ImGui::DragFloat("Fire Hazard DPS", &kFireHazardDps, 0.5f, 0.0f, 50.0f);
#endif
}
