#include "GrenadeLauncher.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

/// <summary>
/// クールダウン・残弾を見て、撃てるならbounces/proximityRadius/blastRadiusを持つ弾1発ぶんの
/// ProjectileSpawnRequest を組み立てて outSpawns に積む。跳ね返り・近接センサー・爆風の実際の
/// 処理はすべて ArcingProjectile / GameScene 側の共通パイプラインが担うので、ここでは
/// 「これらのパラメータを持つ弾を1発リクエストする」以上のことはしない(Blaster.cpp と同じ方針)。
/// </summary>
bool GrenadeLauncher::TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
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
	spawn.lifeTime = kLifeTime;              // フューズ(バウンド回数の上限は設けない)
	spawn.damage = kDamage;
	spawn.knockbackPower = kKnockbackPower;
	spawn.blastRadius = kBlastRadius;
	spawn.bounces = true;
	spawn.wallRestitution = kWallRestitution;
	spawn.floorRestitution = kFloorRestitution; // >0なので床でも跳ね続ける
	spawn.proximityRadius = kBlastRadius;       // 爆風が届く範囲=敵が入ったら起爆する範囲
	outSpawns.push_back(spawn);
	return true;
}

void GrenadeLauncher::DrawImGuiTuning() {
#ifdef USE_IMGUI
	ImGui::DragInt("Starting Ammo", &kStartingAmmo, 1.0f, 1, 20);
	ImGui::DragFloat("Cooldown (s)", &kCooldown, 0.05f, 0.1f, 5.0f);
	ImGui::DragFloat("Muzzle Speed", &kMuzzleSpeed, 0.1f, 1.0f, 40.0f);
	ImGui::DragFloat("Gravity Scale", &kGravityScale, 0.05f, 0.0f, 3.0f);
	ImGui::DragFloat("Bullet Radius", &kRadius, 0.01f, 0.01f, 1.0f);
	ImGui::DragFloat("Life Time / Fuse (s)", &kLifeTime, 0.05f, 0.1f, 10.0f);
	ImGui::DragFloat("Blast Radius", &kBlastRadius, 0.05f, 0.0f, 10.0f);
	ImGui::DragFloat("Wall Restitution", &kWallRestitution, 0.02f, 0.0f, 1.0f);
	ImGui::DragFloat("Floor Restitution", &kFloorRestitution, 0.02f, 0.0f, 1.0f);
	ImGui::Separator();
	ImGui::DragFloat("Damage (direct/center)", &kDamage, 0.5f, 0.0f, 100.0f);
	ImGui::DragFloat("Knockback Power (direct/center)", &kKnockbackPower, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Recoil Power", &kRecoilPower, 0.5f, 0.0f, 50.0f);
#endif
}
