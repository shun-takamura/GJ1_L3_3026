#include "RicochetRifle.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

/// <summary>
/// クールダウン・残弾を見て、撃てるなら弾1発ぶんの ProjectileSpawnRequest を組み立てて
/// outSpawns に積む(Pistol.cpp と同じ単発パターン)。bounces系フィールドを設定している点だけが
/// 他の直撃武器と違う(GrenadeLauncher.cpp 参照。ただし floorRestitution=0 で床には跳ねない)。
/// </summary>
bool RicochetRifle::TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
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
	spawn.bounces = true;
	spawn.wallRestitution = kWallRestitution;
	spawn.floorRestitution = kFloorRestitution;
	spawn.maxBounces = kMaxBounces; // 壁・床合わせて3回反射したら次の接触で着弾する
	outSpawns.push_back(spawn);
	return true;
}

void RicochetRifle::DrawImGuiTuning() {
#ifdef USE_IMGUI
	ImGui::DragInt("Starting Ammo", &kStartingAmmo, 1.0f, 1, 30);
	ImGui::DragFloat("Cooldown (s)", &kCooldown, 0.02f, 0.05f, 3.0f);
	ImGui::DragFloat("Muzzle Speed", &kMuzzleSpeed, 0.1f, 1.0f, 40.0f);
	ImGui::DragFloat("Gravity Scale", &kGravityScale, 0.05f, 0.0f, 3.0f);
	ImGui::DragFloat("Bullet Radius", &kRadius, 0.01f, 0.01f, 1.0f);
	ImGui::DragFloat("Life Time (s)", &kLifeTime, 0.1f, 0.5f, 12.0f);
	ImGui::DragFloat("Wall Restitution", &kWallRestitution, 0.02f, 0.0f, 1.0f);
	ImGui::DragFloat("Floor Restitution", &kFloorRestitution, 0.02f, 0.0f, 1.0f);
	ImGui::DragInt("Max Bounces", &kMaxBounces, 1.0f, 0, 20);
	ImGui::Separator();
	ImGui::DragFloat("Damage", &kDamage, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Knockback Power", &kKnockbackPower, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Recoil Power", &kRecoilPower, 0.5f, 0.0f, 50.0f);
#endif
}
