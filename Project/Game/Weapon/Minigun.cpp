#include "Minigun.h"

#include "Sound/SoundManager.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

Minigun::~Minigun() {
	// held のまま(クリップの途中)で武器が手放された場合、鳴りっぱなしにしない。
	if (fireSoundHandle_ != 0) {
		SoundManager::GetInstance()->Stop3DSound(fireSoundHandle_);
	}
}

/// <summary>
/// クールダウンが切れていて攻撃入力を「押しっぱなし」にしている間、毎フレーム自動で
/// 弾を1発リクエストする(AssaultRifle.cpp と同じ連射パターン。値だけが極端に振ってある)。
/// </summary>
bool Minigun::TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
	float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) {
	(void)triggered; // ミニガンも押しっぱなしで連射するので、トリガーの立ち上がりは見ない(held を使う)

	// 発射音: AssaultRifle.cpp と同じ理由で、弾の発射クールダウンとは切り離して held の間
	// だけループさせる(クリップの実長 kFireSoundDuration が経つたびに鳴らし直し、離した/
	// 弾切れの瞬間に Stop3DSound で即座に止める)。
	const bool shouldLoopFireSound = held && ammo_ > 0;
	if (shouldLoopFireSound) {
		if (fireSoundHandle_ == 0 || fireSoundTimer_ <= 0.0f) {
			// ループの継ぎ目で前回ぶんのハンドルを明示的に止めてから鳴らし直す
			// (通常は自然に鳴り終わっているはずだが、念のため二重再生を防ぐ)。
			if (fireSoundHandle_ != 0) {
				SoundManager::GetInstance()->Stop3DSound(fireSoundHandle_);
			}
			fireSoundHandle_ = SoundManager::GetInstance()->Play3DSound("Minigun_Fire", ownerPos);
			fireSoundTimer_ = kFireSoundDuration;
		} else {
			fireSoundTimer_ -= dt;
			SoundManager::GetInstance()->UpdateEmitter(fireSoundHandle_, ownerPos, { 0.0f, 0.0f, 0.0f });
		}
	} else if (fireSoundHandle_ != 0) {
		SoundManager::GetInstance()->Stop3DSound(fireSoundHandle_);
		fireSoundHandle_ = 0;
	}

	if (cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= dt;
	}
	if (!held || cooldownTimer_ > 0.0f) {
		return false;
	}
	// 本来なら撃てるタイミングだが弾切れの場合、実弾の代わりに空撃ちクリックを鳴らし、
	// クールダウンだけ本来の発射間隔ぶん進める(AssaultRifle.cpp と同じ理由)。
	if (ammo_ <= 0) {
		cooldownTimer_ = kCooldown;
		SoundManager::GetInstance()->Play3DSound("Empty", ownerPos);
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
