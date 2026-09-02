#include "Character.h"

#include <cmath>
#include <utility>

#include "Camera.h"
#include "Common/IStageQuery.h"
#include "Physics/CollisionGeometry.h"
#include "Physics/CollisionSystem.h"
#include "Vector4.h"
#include "Weapon/Weapon.h"
#include "Weapon/UnarmedWeapon.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
	// 当たり判定(Capsule)の姿勢計算に渡す軸。
	// このキャラは常に直立していてヨー回転もさせていないので、単位行列(X/Y/Z軸そのまま)でよい。
	// もし将来キャラを傾ける/横倒しにする演出を作るなら、ここを実際の回転から求め直す必要がある。
	const Vector3 kIdentityAxes[3] = {
		{ 1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f },
	};
}

Character::Character() = default;
Character::~Character() = default;

void Character::Initialize(Camera* camera, const std::string& name, const Vector3& spawnPos) {
	camera_ = camera;
	name_ = name;
	position_ = spawnPos;
	hp_ = kMaxHP;

	// 見た目は仮のBox(キャラクターのビジュアルは未確定のため)。
	// PrimitiveInstance 自身も IImGuiEditable なので、Character とは別に Hierarchy に載る。
	visual_ = std::make_unique<PrimitiveInstance>();
	visual_->Initialize(PrimitiveInstance::PrimitiveType::Box, name_ + "_Visual");
	visual_->SetCamera(camera_);
	visual_->SetScale({ 0.9f, kRestHeight * 2.0f, 0.9f }); // 高さ = kRestHeight*2 (中心が kRestHeight のとき足元がちょうど y=0 に来る)
	visual_->SetTranslate(position_);

	SetupCollider();

	// 初期装備は常に素手(UnarmedWeapon)。equippedWeapon_ が nullptr になる瞬間を作らないことで、
	// Update() 側は「武器を持っていない」という特別分岐を考えずに済む(Weapon.h の設計コメント参照)。
	equippedWeapon_ = std::make_unique<UnarmedWeapon>();
}

void Character::Finalize() {
	visual_.reset();
}

void Character::SetupCollider() {
	// CollisionSystem::ColliderOf は「無ければ作る」ので、Register を明示的に呼ぶ必要はない
	// (IImGuiEditable のコンストラクタで発火するライフサイクルフック経由で、GameApp が
	// 既に自動登録してくれている。01_GettingStarted.md / GameApp::Initialize 参照)。
	Collider& c = CollisionSystem::GetInstance()->ColliderOf(this);
	c.shape = ColliderShape::Capsule;
	c.capsuleRadius = kCapsuleRadius;
	c.capsuleHeight = kCapsuleHeight;
	c.offset = { 0.0f, 0.0f, 0.0f }; // position_ がそのままカプセルの中心になる
	c.enabled = true;                // これを true にし忘れると判定されないので注意(エンジンの定番の落とし穴)
	c.showDebug = true;              // Debugビルドでカプセルのワイヤーフレームが見える(見た目の Box とは別形状なので、当たり判定を目視確認するのに使う)
	c.onCollision = [this](IImGuiEditable* other) { ResolveBodyBlock(other); };
}

void Character::ResolveBodyBlock(IImGuiEditable* other) {
	// これは「素手攻撃のヒット判定」とは別物。CollisionSystem::Update() が毎フレーム
	// 全キャラの Capsule 同士を総当たりでチェックしていて、実際に3Dで重なっていると
	// 判定されたときだけこのコールバックが呼ばれる(=呼ばれた時点で「本当に重なっている」ことは確定済み)。
	// なのでここでは重なっているかどうかを再チェックする必要はなく、
	// 横視点らしくX方向だけに押し離せばよい(Z は常に固定、Y は重力任せなので触らない)。
	Vector3* otherPos = other->GetEditableTranslate();
	if (!otherPos) {
		return; // 相手が translate を持たない(＝当たり判定の対象になり得ない)エンティティだった場合の保険
	}

	float deltaX = position_.x - otherPos->x;
	float awayX = (deltaX >= 0.0f) ? 1.0f : -1.0f; // 自分が相手より右にいれば+1(右へ押される)、左にいれば-1
	// onCollision コールバックには dt が渡ってこないため、正確な「めり込み量ぶんだけ押し戻す」
	// 処理はできない。代わりに、重なっている間は毎フレーム固定の小さい量だけ押し離す
	// 簡易的な解決方法にしている(見た目上は自然にすり抜け防止になる)。
	// この処理は当たっている両方のキャラで同時に走る(相手側も自分の onCollision で同じことをする)ので、
	// 押す量は片側ぶんだけで足りる。
	constexpr float kBodyPushPerFrame = 0.03f;
	position_.x += awayX * kBodyPushPerFrame;
}

void Character::Update(float dt, float moveX, bool jumpTriggered, bool crouchHeld,
	float aimDirX, float aimDirY, bool attackTriggered, bool attackHeld, bool throwTriggered) {
	// このフレームの移動前の位置。地形当たり判定は「開始位置 → 積分後の位置」で一度だけ解決する。
	const Vector3 startPos = position_;

	// ---- しゃがみ判定 ----
	// 接地中にしゃがみ入力があればしゃがむ。入力を離しても、頭上に立ち上がる空間が
	// 無ければしゃがみを継続する(低い隙間の下で勝手に立って天井へめり込むのを防ぐ)。
	bool wantCrouch = crouchHeld && grounded_;
	if (!wantCrouch && isCrouching_ && stage_) {
		const Vector3 standHalf{ kCapsuleRadius, kRestHeight, kCapsuleRadius };
		if (stage_->OverlapsSolid(position_, standHalf)) {
			wantCrouch = true; // つっかえて立てない
		}
	}
	isCrouching_ = wantCrouch;

	// ---- 左右移動(X軸のみ。横視点なので奥行き方向には動かない) ----
	// しゃがみ中も移動できる(しゃがみ歩き)。ただし速度は kCrouchMoveScale 倍に落ちる。
	{
		// アナログスティックは magnitude 込みで渡ってくるので理論上 1.0 を超えないはずだが、
		// キーボードと合算する呼び出し側の実装次第では超える可能性もあるため念のためクランプする。
		if (moveX > 1.0f) moveX = 1.0f;
		if (moveX < -1.0f) moveX = -1.0f;
		if (moveX > 0.0001f || moveX < -0.0001f) {
			const float speed = kMoveSpeed * (isCrouching_ ? kCrouchMoveScale : 1.0f);
			position_.x += moveX * speed * dt;
		}
	}

	// ---- ノックバック(X方向のみ。時間経過で自然に0へ減衰する) ----
	// しゃがみ中でも(自分の意思による移動とは無関係に)ノックバックはそのまま適用する。
	position_.x += knockbackVelocityX_ * dt;
	float damping = 1.0f - kKnockbackDamping * dt;
	if (damping < 0.0f) damping = 0.0f; // dt が大きすぎて減衰が負になる(＝逆向きに加速する)事故を防ぐ
	knockbackVelocityX_ *= damping;

	// ---- 重力・ジャンプ ----
	// しゃがみ中はジャンプできない(しゃがみを解除してから)。
	if (jumpTriggered && grounded_ && !isCrouching_) {
		verticalVelocity_ = kJumpSpeed;
		grounded_ = false;
	}
	verticalVelocity_ += kGravity * dt; // 重力を毎フレーム加速度として積分
	position_.y += verticalVelocity_ * dt;

	// ---- 地形との当たり判定 ----
	if (stage_) {
		// 当たり判定 AABB は見た目の Box と同じ寸法。しゃがみ中は頭が下がるぶん高さを縮め、
		// 足元(position_.y - kRestHeight)は動かさない。position_ は「立ち姿勢での中心」基準なので、
		// しゃがみ中は当たり判定の中心を centerYOffset だけ下げて計算し、結果を戻すときに足す。
		const float heightScale = isCrouching_ ? kCrouchHeightScale : 1.0f;
		const float centerYOffset = -kRestHeight * (1.0f - heightScale);
		const Vector3 half{ kCapsuleRadius, kRestHeight * heightScale, kCapsuleRadius };
		const Vector3 fromC{ startPos.x, startPos.y + centerYOffset, startPos.z };
		const Vector3 toC{ position_.x, position_.y + centerYOffset, position_.z };
		const StageMoveResult mv = stage_->MoveAabb(fromC, toC, half);
		position_ = { mv.position.x, mv.position.y - centerYOffset, mv.position.z };
		if (mv.grounded) {
			if (verticalVelocity_ < 0.0f) verticalVelocity_ = 0.0f; // 落下を止める(上向き初速は残さない)
			grounded_ = true;
		} else {
			grounded_ = false;
		}
		if (mv.hitCeiling && verticalVelocity_ > 0.0f) {
			verticalVelocity_ = 0.0f; // 天井に頭をぶつけたら上昇を止める
		}
		if (mv.hitWall) {
			knockbackVelocityX_ = 0.0f; // 壁にめり込むノックバックはそこで止める
		}
	} else {
		// stage 未設定時のフォールバック: 常に y=kRestHeight に平床がある前提。
		// (Character 単体テストや、ステージ差し替え前の暫定動作用)
		if (position_.y <= kRestHeight) {
			position_.y = kRestHeight;
			verticalVelocity_ = 0.0f;
			grounded_ = true;
		}
	}

	// ---- 照準方向の更新 ----
	// GameScene 側でほぼ正規化済みのはずだが、念のためここでも正規化する。
	// マウスがちょうどキャラの真上にあるなど、方向が定まらない(ほぼ0ベクトルの)フレームは
	// 直前の照準方向を維持する(急にパンチの向きが原点にすっ飛ぶのを防ぐ)。
	const float aimLenSq = aimDirX * aimDirX + aimDirY * aimDirY;
	if (aimLenSq > 0.0001f) {
		const float invLen = 1.0f / std::sqrt(aimLenSq);
		aimDirX_ = aimDirX * invLen;
		aimDirY_ = aimDirY * invLen;
	}

	// ---- 攻撃(素手 or 装備中の武器) ----
	// 実際の攻撃ロジック(クールダウン・残弾・弾道)は装備中の Weapon に委譲する。
	// Character は「今 hitbox/弾が生成されたかどうか」を受け取ってペンディングバッファに
	// 積むだけで、武器ごとの違いは一切知らない(Weapon.h の設計コメント参照)。
	AttackHitbox meleeHitbox;
	if (equippedWeapon_->TryMeleeAttack(dt, attackTriggered, position_, aimDirX_, aimDirY_, meleeHitbox)) {
		hasPendingAttack_ = true;
		pendingAttack_ = meleeHitbox;
	}
	std::vector<ProjectileSpawnRequest> spawns;
	if (equippedWeapon_->TryRangedAttack(dt, attackTriggered, attackHeld, position_, aimDirX_, aimDirY_, spawns)) {
		for (const ProjectileSpawnRequest& spawn : spawns) {
			pendingProjectileSpawns_.push_back(spawn);
		}
		// 反動: 発射方向と逆向きに軽くノックバックする(武器ごとの大きさは Weapon 側が持つ)。
		ApplyKnockback(-aimDirX_, equippedWeapon_->GetRecoilPower());
	}

	// ---- 投げ捨て ----
	// 素手(CanBeThrown() == false)のときは何も起きない。投げた瞬間に装備は素手へ戻る。
	// 投げた武器の中身(残弾等)は問わない ── ダメージ・ノックバックは kThrow* の固定値を使う
	// (「弾切れの銃でも投げれば同じ威力」という仕様。Weapon.h 側の反動とは無関係の別パラメータ)。
	if (throwTriggered && equippedWeapon_->CanBeThrown()) {
		// 投げる位置は自分の中心から照準方向へ少し離す(自分自身に当たらないようにするため)。
		pendingThrow_.origin = { position_.x + aimDirX_ * kThrowForwardOffset, position_.y + aimDirY_ * kThrowForwardOffset, position_.z };
		// 初速は照準方向 × 投擲速度。この後は ArcingProjectile 側が重力を積分して放物線を描く
		// (銃弾と全く同じ物理。Weapon/ArcingProjectile.h 参照)。
		pendingThrow_.velocityX = aimDirX_ * kThrowSpeed;
		pendingThrow_.velocityY = aimDirY_ * kThrowSpeed;
		pendingThrow_.gravityScale = kThrowGravityScale;
		pendingThrow_.radius = kThrowRadius;
		pendingThrow_.lifeTime = kThrowLifeTime;         // 何にも当たらなければこの秒数で消える
		pendingThrow_.damage = kThrowDamage;             // 命中時のダメージ(投げ武器固定値)
		pendingThrow_.knockbackPower = kThrowKnockbackPower;
		hasPendingThrow_ = true; // GameScene が ConsumePendingThrow() で回収し、実体(ArcingProjectile)を生成する
		// 今の武器は GameScene 側で使い捨ての飛翔体になる(拾い直せる物として地面に残ることはない)ので、
		// ここで所有権を手放し、素手に持ち替える。
		equippedWeapon_ = std::make_unique<UnarmedWeapon>();
	}

	// ---- ダメージフラッシュ(ApplyDamage で damageFlashTimer_ がセットされている間、赤くする) ----
	if (damageFlashTimer_ > 0.0f) {
		damageFlashTimer_ -= dt;
	}

	// ---- 見た目への反映 ----
	if (visual_) {
		// しゃがみ中は Box を上から縮めて見た目だけ低くする。足元(y=0)は動かさず、
		// 頭の高さだけが下がるように中心Y(visualPos.y)を計算する。
		// 立っているとき(heightScale=1)は補正項が0になり、position_.y をそのまま使う
		// (＝ジャンプ中の弧はこのロジックの影響を受けない)。
		const float heightScale = isCrouching_ ? kCrouchHeightScale : 1.0f;
		visual_->SetScale({ 0.9f, kRestHeight * 2.0f * heightScale, 0.9f });
		Vector3 visualPos = position_;
		visualPos.y = position_.y - kRestHeight * (1.0f - heightScale);
		visual_->SetTranslate(visualPos);
		// ダメージを受けた直後だけ赤く光らせる(それ以外は素の白)。
		// 「当たったのに何も起きた感じがしない」を防ぐための最小限の反応。
		visual_->GetMesh().SetColor(damageFlashTimer_ > 0.0f
			? Vector4{ 1.0f, 0.2f, 0.2f, 1.0f }
			: Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
		visual_->Update();
	}
}

void Character::Draw() {
	if (visual_) {
		visual_->Draw();
	}
}

bool Character::ConsumePendingAttack(AttackHitbox& outHitbox) {
	if (!hasPendingAttack_) {
		return false;
	}
	outHitbox = pendingAttack_;
	hasPendingAttack_ = false; // 1回取り出したら消費済み。次に攻撃するまで false のまま
	return true;
}

bool Character::ReceiveHit(const AttackHitbox& hitbox) {
	if (IsDead()) {
		return false; // 死亡済みキャラは追加でダメージ/ノックバックを受けない
	}

	// 相手の攻撃判定球(sphere)と、自分の当たり判定カプセルが実際に重なっているかを判定する。
	// CollisionSystem の総当たりループには乗せず、ここで CollisionGeometry を直接呼んでいる
	// (06_Collision.md が推奨する「自前でCollisionGeometryを直接使う」パターン)。
	const bool hit = CollisionGeometry::TestSphereCapsule(
		hitbox.center, hitbox.radius,
		position_, kIdentityAxes, kCapsuleHeight, kCapsuleRadius);
	if (!hit) {
		return false;
	}

	ApplyDamage(hitbox.damage);
	// ノックバックは「攻撃した瞬間の攻撃側の向き」をそのまま使う(hitbox.knockbackDirX)。
	// 自分の位置と命中位置(hitbox.center)から向きを逆算すると、密着距離では
	// 攻撃ヒットボックスの中心が自分を追い越してしまい、向きが反転するバグになるため。
	ApplyKnockback(hitbox.knockbackDirX, hitbox.knockbackPower);
	return true;
}

void Character::EquipWeapon(std::unique_ptr<Weapon> weapon) {
	equippedWeapon_ = std::move(weapon);
}

bool Character::CanPickUpWeapon() const {
	// 「投げ捨てられない武器を今持っている」＝素手、という判定にすることで、
	// Unarmed かどうかを直接見るための特別なフラグを別に持たずに済む。
	return !equippedWeapon_->CanBeThrown();
}

std::string Character::GetEquippedWeaponName() const {
	return equippedWeapon_->GetName();
}

int Character::GetEquippedAmmo() const {
	return equippedWeapon_->GetRemainingAmmo();
}

bool Character::ConsumePendingProjectileSpawns(std::vector<ProjectileSpawnRequest>& outSpawns) {
	if (pendingProjectileSpawns_.empty()) {
		return false;
	}
	outSpawns = std::move(pendingProjectileSpawns_);
	pendingProjectileSpawns_.clear(); // move後の状態は未規定なので、明示的に空にしておく
	return true;
}

bool Character::ConsumePendingThrow(ProjectileSpawnRequest& outSpawn) {
	if (!hasPendingThrow_) {
		return false;
	}
	outSpawn = pendingThrow_;
	hasPendingThrow_ = false; // 1回取り出したら消費済み。次に投げるまで false のまま
	return true;
}

void Character::ApplyDamage(float amount) {
	hp_ -= amount;
	if (hp_ < 0.0f) {
		hp_ = 0.0f; // HPは負にしない(0以下=死亡は IsDead() が見る)
	}
	if (amount > 0.0f) {
		damageFlashTimer_ = kDamageFlashDuration; // Update() 側でこの秒数だけ赤く表示する
	}
}

void Character::ApplyKnockback(float directionX, float power) {
	// directionX は連続値で渡ってくる可能性があるが、このゲームでは
	// 「左右どちらへ飛ぶか」の符号だけが意味を持つので、±1に丸めてから使う。
	float dirX = (directionX >= 0.0f) ? 1.0f : -1.0f;
	knockbackVelocityX_ = dirX * power; // 既存のノックバック速度を上書きする(積み増しはしない)
}

void Character::ResetForNewRound(const Vector3& spawnPos) {
	// HP・速度・しゃがみ/接地状態・攻撃クールダウンをすべて初期状態に戻し、spawnPos へ再配置する。
	// 本物の「ラウンド進行」(10ポイント先取・次ステージ選出など)はフェーズ5の別タスクで、
	// これはあくまで GameScene がその場でテストを続けられるようにするための仮リセット。
	hp_ = kMaxHP;
	position_ = spawnPos;
	knockbackVelocityX_ = 0.0f;
	verticalVelocity_ = 0.0f;
	grounded_ = true;
	hasPendingAttack_ = false;
	pendingProjectileSpawns_.clear();
	hasPendingThrow_ = false;
	damageFlashTimer_ = 0.0f;

	if (visual_) {
		visual_->SetTranslate(position_);
		visual_->GetMesh().SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // 赤フラッシュが残ったまま次ラウンドへ持ち越さない
	}
}

void Character::OnImGuiInspector() {
#ifdef USE_IMGUI
	// Inspector でこのキャラを選択したときに出るデバッグ用のミニパネル。
	// HP・接地状態の確認と、位置の直接編集、即死ボタン(HP0にしてリセット動作を試す用)を提供する。
	ImGui::Text("HP: %.0f / %.0f", hp_, kMaxHP);
	ImGui::Text("Grounded: %s", grounded_ ? "true" : "false");
	const int ammo = GetEquippedAmmo();
	if (ammo == Weapon::kInfiniteAmmo) {
		ImGui::Text("Weapon: %s", GetEquippedWeaponName().c_str());
	} else {
		ImGui::Text("Weapon: %s (Ammo: %d)", GetEquippedWeaponName().c_str(), ammo);
	}
	ImGui::DragFloat3("Position", &position_.x, 0.1f);
	if (ImGui::Button("Kill")) {
		ApplyDamage(hp_);
	}
#endif
}
