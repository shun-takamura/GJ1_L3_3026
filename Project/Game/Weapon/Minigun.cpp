#include "Minigun.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

/// <summary>
/// クールダウンが切れていて攻撃入力を「押しっぱなし」にしている間、毎フレーム自動で
/// 弾を1発リクエストする(AssaultRifle.cpp と同じ連射パターン。値だけが極端に振ってある)。
/// </summary>
bool Minigun::TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
	float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) {
	(void)triggered; // ミニガンも押しっぱなしで連射するので、トリガーの立ち上がりは見ない(held を使う)

	if (cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= dt;
	}
	if (!held || cooldownTimer_ > 0.0f || ammo_ <= 0) {
		return false; // 弾切れでも武器はそのまま(自動では捨てない)。撃てないだけ
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
	outSpawns.push_back(spawn);
	return true;
}

void Minigun::DrawImGuiTuning() {
#ifdef USE_IMGUI
	ImGui::DragInt("Starting Ammo", &kStartingAmmo, 1.0f, 1, 300);
	ImGui::DragFloat("Cooldown (s)", &kCooldown, 0.005f, 0.01f, 1.0f);
	ImGui::DragFloat("Muzzle Speed", &kMuzzleSpeed, 0.1f, 1.0f, 40.0f);
	ImGui::DragFloat("Gravity Scale", &kGravityScale, 0.05f, 0.0f, 3.0f);
	ImGui::DragFloat("Bullet Radius", &kRadius, 0.01f, 0.01f, 1.0f);
	ImGui::DragFloat("Life Time (s)", &kLifeTime, 0.05f, 0.1f, 10.0f);
	ImGui::Separator();
	ImGui::DragFloat("Damage", &kDamage, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Knockback Power", &kKnockbackPower, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Recoil Power", &kRecoilPower, 0.5f, 0.0f, 50.0f);
#endif
}
