#include "AssaultRifle.h"

#include "Sound/SoundManager.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

AssaultRifle::~AssaultRifle() {
	// held のまま(クリップの途中)で武器が手放された場合、鳴りっぱなしにしない。
	if (fireSoundHandle_ != 0) {
		SoundManager::GetInstance()->Stop3DSound(fireSoundHandle_);
	}
}

/// <summary>
/// クールダウンが切れていて攻撃入力を「押しっぱなし」にしている間、毎フレーム自動で
/// 弾を1発リクエストする(Pistol/Shotgun と違い triggered ではなく held を見る)。
/// </summary>
bool AssaultRifle::TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
	float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) {
	(void)triggered; // ARは押しっぱなしで連射するので、トリガーの立ち上がりは見ない(held を使う)

	// 発射音: 弾の発射クールダウン(kCooldown、連射レート)とは切り離して、held の間だけ
	// ループさせる。SoundManager に再生完了を問い合わせる手段が無いため、クリップの実長
	// (kFireSoundDuration)を自前のタイマーで計って「終わったらまた鳴らす」を組んでいる。
	// 離した瞬間(または弾切れ)は鳴っている途中でも Stop3DSound で即座に止める
	// (毎ショットPlayし直して音が重なる/離しても最後まで鳴り続ける、という以前の挙動を解消)。
	const bool shouldLoopFireSound = held && ammo_ > 0;
	if (shouldLoopFireSound) {
		if (fireSoundHandle_ == 0 || fireSoundTimer_ <= 0.0f) {
			// ループの継ぎ目で前回ぶんのハンドルを明示的に止めてから鳴らし直す
			// (通常は自然に鳴り終わっているはずだが、念のため二重再生を防ぐ)。
			if (fireSoundHandle_ != 0) {
				SoundManager::GetInstance()->Stop3DSound(fireSoundHandle_);
			}
			fireSoundHandle_ = SoundManager::GetInstance()->Play3DSound("AssaultRifle_Fire", ownerPos);
			fireSoundTimer_ = kFireSoundDuration;
		} else {
			fireSoundTimer_ -= dt;
			SoundManager::GetInstance()->UpdateEmitter(fireSoundHandle_, ownerPos, { 0.0f, 0.0f, 0.0f });
		}
	} else if (fireSoundHandle_ != 0) {
		SoundManager::GetInstance()->Stop3DSound(fireSoundHandle_);
		fireSoundHandle_ = 0;
	}

	// クールダウン中は毎フレーム減らしていくだけ。held が true のままなら、
	// クールダウンが切れた瞬間に自動でまた発射が成立する(＝連射になる)。
	if (cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= dt;
	}
	// 押していない、またはまだクールダウン中なら何もしない(弾切れかどうかはまだ問わない)。
	if (!held || cooldownTimer_ > 0.0f) {
		return false;
	}
	// ここまで来た時点で「本来なら撃てるタイミング」。弾切れなら、実弾の代わりに
	// 空撃ちクリックを鳴らし、クールダウンだけ本来の発射間隔ぶん進めておく ──
	// これにより held を保持している間、AssaultRifle自身の連射レート(kCooldown)と
	// 同じ間隔でクリック音が鳴り続ける(ユーザー要望:「銃ごとに発射間隔でなってほしい」)。
	if (ammo_ <= 0) {
		cooldownTimer_ = kCooldown;
		SoundManager::GetInstance()->Play3DSound("Empty", ownerPos);
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
	spawn.gravityScale = kGravityScale; // 重力の掛かり具合(1.0未満なので Pistol より弾道が平ら)
	spawn.radius = kRadius;             // 弾の当たり判定半径
	spawn.lifeTime = kLifeTime;         // 何にも当たらなくてもこの秒数で消える
	spawn.damage = kDamage;             // 命中時のダメージ(1発は低威力、連射で補う想定)
	spawn.knockbackPower = kKnockbackPower; // 命中時のノックバックの大きさ
	outSpawns.push_back(spawn);
	return true;
}

void AssaultRifle::DrawImGuiTuning() {
#ifdef USE_IMGUI
	ImGui::DragInt("Starting Ammo", &kStartingAmmo, 1.0f, 1, 200);
	ImGui::DragFloat("Cooldown (s)", &kCooldown, 0.01f, 0.02f, 1.0f);
	ImGui::DragFloat("Muzzle Speed", &kMuzzleSpeed, 0.1f, 1.0f, 40.0f);
	ImGui::DragFloat("Gravity Scale", &kGravityScale, 0.05f, 0.0f, 3.0f);
	ImGui::DragFloat("Bullet Radius", &kRadius, 0.01f, 0.01f, 1.0f);
	ImGui::DragFloat("Life Time (s)", &kLifeTime, 0.05f, 0.1f, 10.0f);
	ImGui::Separator();
	ImGui::DragFloat("Damage", &kDamage, 0.5f, 0.0f, 100.0f);
	ImGui::DragFloat("Knockback Power", &kKnockbackPower, 0.5f, 0.0f, 50.0f);
	ImGui::DragFloat("Recoil Power", &kRecoilPower, 0.5f, 0.0f, 50.0f);
#endif
}
