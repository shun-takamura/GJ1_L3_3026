#include "Character.h"

#include "Camera.h"
#include "Physics/CollisionGeometry.h"
#include "Physics/CollisionSystem.h"

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

void Character::Update(float dt, float moveX, bool jumpTriggered, bool crouchHeld, bool attackTriggered) {
	// しゃがみは接地中のみ成立させる(空中でしゃがみ入力を押しても姿勢は変わらない)。
	// しゃがみ中は下の各ブロックで移動・ジャンプをブロックする。
	isCrouching_ = crouchHeld && grounded_;

	// ---- 左右移動(X軸のみ。横視点なので奥行き方向には動かない) ----
	if (!isCrouching_) {
		// アナログスティックは magnitude 込みで渡ってくるので理論上 1.0 を超えないはずだが、
		// キーボードと合算する呼び出し側の実装次第では超える可能性もあるため念のためクランプする。
		if (moveX > 1.0f) moveX = 1.0f;
		if (moveX < -1.0f) moveX = -1.0f;
		if (moveX > 0.0001f || moveX < -0.0001f) {
			facingX_ = (moveX > 0.0f) ? 1.0f : -1.0f; // 攻撃の前方判定はこの向きを使う
			position_.x += moveX * kMoveSpeed * dt;
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
	if (position_.y <= kRestHeight) {
		// 床(y=0平面)に着地。本当は地形の高さを見るべきだが、フェーズ1では「常に平らな床がある」
		// という単純化をしている(場外は GameScene 側が X座標だけで別途判定する)。
		position_.y = kRestHeight;
		verticalVelocity_ = 0.0f;
		grounded_ = true;
	}

	// ---- 攻撃 ----
	// ここでやるのはクールダウン管理と「攻撃ヒットボックスの生成」だけ。
	// 実際に「当たったかどうか」の判定は行わない(ReceiveHit 側の責務)。
	// 理由: このキャラは「相手が誰か」を知らないので、ここで判定のしようがない。
	if (attackCooldownTimer_ > 0.0f) {
		attackCooldownTimer_ -= dt;
	}
	if (attackTriggered && attackCooldownTimer_ <= 0.0f) {
		attackCooldownTimer_ = kAttackCooldown;
		hasPendingAttack_ = true;
		// 攻撃判定球は、自分の位置から向いている方向(facingX_)へ kAttackForwardOffset だけ
		// 離した位置に置く(＝正面を殴るイメージ)。Y/Z は自分と同じ高さ・同じ奥行き。
		pendingAttack_.center = { position_.x + facingX_ * kAttackForwardOffset, position_.y, position_.z };
		pendingAttack_.radius = kAttackRadius;
		pendingAttack_.damage = kAttackDamage;
		pendingAttack_.knockbackPower = kAttackKnockbackPower;
		// ノックバックの向きは「今の自分の向き」をそのまま持たせる。
		// center(命中位置)から逆算すると、密着距離ではヒットボックス中心が相手を追い越してしまい
		// 符号が反転する(＝殴った側に向かって吹っ飛ぶ)バグになるため、あえて逆算しない。
		pendingAttack_.knockbackDirX = facingX_;
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

void Character::ApplyDamage(float amount) {
	hp_ -= amount;
	if (hp_ < 0.0f) {
		hp_ = 0.0f; // HPは負にしない(0以下=死亡は IsDead() が見る)
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
	attackCooldownTimer_ = 0.0f;
	hasPendingAttack_ = false;

	if (visual_) {
		visual_->SetTranslate(position_);
	}
}

void Character::OnImGuiInspector() {
#ifdef USE_IMGUI
	// Inspector でこのキャラを選択したときに出るデバッグ用のミニパネル。
	// HP・接地状態の確認と、位置の直接編集、即死ボタン(HP0にしてリセット動作を試す用)を提供する。
	ImGui::Text("HP: %.0f / %.0f", hp_, kMaxHP);
	ImGui::Text("Grounded: %s", grounded_ ? "true" : "false");
	ImGui::DragFloat3("Position", &position_.x, 0.1f);
	if (ImGui::Button("Kill")) {
		ApplyDamage(hp_);
	}
#endif
}
