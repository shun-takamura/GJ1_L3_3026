#include "Character.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

#include "Camera.h"
#include "Common/IStageQuery.h"
#include "Physics/CollisionGeometry.h"
#include "Physics/CollisionSystem.h"
#include "Vector4.h"
#include "Weapon/Weapon.h"
#include "Weapon/UnarmedWeapon.h"
#include "Object3DInstance.h"
#include "ModelManager.h"
#include "AnimatedModelInstance.h"
#include "AnimatedObject3DInstance.h"

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

	// アニメクリップ。cook 後は Resources/Models/Player/player_<名前>.anim になる。
	// 並びは Blender の generate_anims.py の CLIP_NAMES と一致させること。
	enum {
		CLIP_IDLE, CLIP_RUN, CLIP_JUMP, CLIP_FALL, CLIP_LAND,
		CLIP_SHOOT, CLIP_PUNCH, CLIP_THROW, CLIP_HIT, CLIP_DEATH,
		CLIP_WALLSLIDE, CLIP_WALLJUMP, CLIP_CROUCHIDLE, CLIP_CROUCHWALK,
		CLIP_COUNT
	};
	const char* const kClipNames[CLIP_COUNT] = {
		"Idle", "Run", "Jump", "Fall", "Land",
		"Shoot", "Punch", "Throw", "Hit", "Death",
		"WallSlide", "WallJump", "CrouchIdle", "CrouchWalk",
	};

	// 全キャラ共有の向き補正(度)。Blender→エンジンの軸ずれを実機で詰めるための調整値。
	// モデルは既定で +X(画面右)向き。aimDirX_ が負のとき 180° 足して左を向かせる。
	float s_modelYawOffsetDeg = 0.0f;

	// 予備動作の種類(Character::windupKind_)。
	enum { WU_NONE, WU_JUMP, WU_THROW, WU_MELEE };

	constexpr const char* kAnimMeshPath = "Resources/Models/Player/player.mesh";

	std::string ClipPath(int clip) {
		return std::string("Resources/Models/Player/player_") + kClipNames[clip] + ".anim";
	}
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
	animChara_.reset();   // animModel_ を生ポインタで参照しているので先に破棄
	animModel_.reset();
	visual_.reset();
	weaponModel_.reset();
}

void Character::SetWeaponRenderContext(Object3DManager* object3DManager, DirectXCore* dxCore) {
	object3DManager_ = object3DManager;
	weaponModelDxCore_ = dxCore;
}

void Character::SetupAnimatedModel(Object3DManager* object3DManager,
	SkinningComputeManager* skinningComputeManager, DirectXCore* dxCore, SRVManager* srvManager,
	const Vector4& teamColor) {
	object3DManagerForAnim_ = object3DManager;
	skinningComputeManager_ = skinningComputeManager;
	animDxCore_ = dxCore;
	srvManager_ = srvManager;
	teamColor_ = teamColor;

	// アセット未生成(cook 前)でもゲームは Box のまま動くようにする。
	if (!object3DManager || !skinningComputeManager || !dxCore || !srvManager) return;
	if (!std::filesystem::exists(kAnimMeshPath)) return;

	animModel_ = std::make_unique<AnimatedModelInstance>();
	animModel_->Initialize(ModelManager::GetInstance()->GetModelCore(),
		"Resources/Models/Player", "player.mesh");

	animChara_ = std::make_unique<AnimatedObject3DInstance>();
	animChara_->Initialize(object3DManager, skinningComputeManager, dxCore, srvManager,
		animModel_.get(), name_ + "_Model");
	animChara_->SetSourcePath("Resources/Models/Player", "player.mesh");
	animChara_->SetCamera(camera_);
	animChara_->SetScale({ kModelScale, kModelScale, kModelScale });
	animChara_->SetMaterialColor(teamColor_);

	currentClipIndex_ = CLIP_IDLE;
	if (std::filesystem::exists(ClipPath(CLIP_IDLE))) {
		animChara_->PlayAnimation(ClipPath(CLIP_IDLE), 0.0f);
		animChara_->SetLoop(true);
	}
	// これ以降、Character::Draw() は Box を描かず animChara_ を描く。
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

int Character::DetectWallContact() const {
	if (!stage_) {
		return 0; // 平床フォールバックには壁の概念が無い
	}
	// 姿勢に合わせた当たり判定の中心・半高(MoveAabb に渡しているものと同じ考え方)。
	const float heightScale = isCrouching_ ? kCrouchHeightScale : 1.0f;
	const float centerYOffset = -kRestHeight * (1.0f - heightScale);
	// 床・天井を「壁」と誤検出しないよう、プローブは体より少し薄い縦幅にする。
	const Vector3 probeHalf{ kWallProbeReach, kRestHeight * heightScale * 0.6f, kCapsuleRadius };
	const float cy = position_.y + centerYOffset;
	// 体の側面のすぐ外側(kCapsuleRadius + kWallProbeReach ぶん外)に solid セルがあるか。
	const Vector3 rightC{ position_.x + kCapsuleRadius + kWallProbeReach, cy, position_.z };
	if (stage_->OverlapsSolid(rightC, probeHalf)) {
		return +1;
	}
	const Vector3 leftC{ position_.x - kCapsuleRadius - kWallProbeReach, cy, position_.z };
	if (stage_->OverlapsSolid(leftC, probeHalf)) {
		return -1;
	}
	return 0;
}

bool Character::HasSolidBelow(float reach) const {
	if (!stage_) {
		return true; // 平床フォールバックでは常に床がある扱い
	}
	// 足元(position_.y - kRestHeight)から下へ reach ぶんの薄い箱に solid セルが重なるか。
	const Vector3 half{ kCapsuleRadius * 0.9f, reach * 0.5f, kCapsuleRadius };
	const Vector3 center{ position_.x, position_.y - kRestHeight - reach * 0.5f, position_.z };
	return stage_->OverlapsSolid(center, half);
}

void Character::Update(float dt, float moveX, bool jumpTriggered, bool crouchHeld,
	float aimDirX, float aimDirY, bool attackTriggered, bool attackHeld, bool throwTriggered) {
	// このフレームの移動前の位置。地形当たり判定は「開始位置 → 積分後の位置」で一度だけ解決する。
	const Vector3 startPos = position_;

	// ---- 予備動作(windup)の進行 ----
	// アニメの「実際に動く」フレームにゲーム内効果を合わせるための遅延。
	// windupTimer_ が 0 を跨いだフレームで、下の各セクションが効果を発動する。
	if (IsDead()) {
		windupKind_ = WU_NONE;  // 予備動作中に死んだら発動しない
	}
	if (windupKind_ != WU_NONE) {
		windupTimer_ -= dt;
	}
	if (windupKind_ == WU_JUMP) {
		// 踏み切りモーション中は足を止め、追加入力を無視する(しゃがみジャンプの溜め)。
		moveX = 0.0f;
		jumpTriggered = false;
		crouchHeld = false;
	}

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

	// ---- 壁接触の検出 / 壁ジャンプ直後の入力ロック ----
	// この時点の position_ はまだこのフレームの移動を積分していない(startPos と同じ)。
	// 左右どちらの壁に密着しているかを先に確定させ、壁ジャンプ・壁ずり落ちの判定に使う。
	wallContactDir_ = DetectWallContact();
	wallSliding_ = false;
	if (wallJumpInputLockTimer_ > 0.0f) {
		wallJumpInputLockTimer_ -= dt;
		// 壁ジャンプ直後は、蹴った壁の方向へ入力しても少しの間は無視する
		// (壁へ張り付き直して連続で登れてしまうのを防ぐ&反対方向へ流す猶予を作る)。
		if (wallJumpLockDir_ > 0 && moveX > 0.0f) moveX = 0.0f;
		else if (wallJumpLockDir_ < 0 && moveX < 0.0f) moveX = 0.0f;
	}
	// 壁方向へ移動入力しているか(壁ジャンプ・壁ずり落ちの共通条件)。上の入力ロック適用後の moveX で見る。
	const bool pressingIntoWall =
		(wallContactDir_ > 0 && moveX > 0.0001f) || (wallContactDir_ < 0 && moveX < -0.0001f);

	// ---- 左右移動(X軸のみ。横視点なので奥行き方向には動かない) ----
	// しゃがみ中も移動できる(しゃがみ歩き)。ただし速度は kCrouchMoveScale 倍に落ちる。
	{
		// アナログスティックは magnitude 込みで渡ってくるので理論上 1.0 を超えないはずだが、
		// キーボードと合算する呼び出し側の実装次第では超える可能性もあるため念のためクランプする。
		if (moveX > 1.0f) moveX = 1.0f;
		if (moveX < -1.0f) moveX = -1.0f;
		if (moveX > 0.0001f || moveX < -0.0001f) {
			// slowMultiplier_ は氷銃で1.0未満になる(ApplySlow参照)。通常時は1.0で無効。
			const float speed = kMoveSpeed * (isCrouching_ ? kCrouchMoveScale : 1.0f) * slowMultiplier_;
			position_.x += moveX * speed * dt;
		}
	}

	// ---- ノックバック(X方向のみ。時間経過で自然に0へ減衰する) ----
	// しゃがみ中でも(自分の意思による移動とは無関係に)ノックバックはそのまま適用する。
	position_.x += knockbackVelocityX_ * dt;
	float damping = 1.0f - kKnockbackDamping * dt;
	if (damping < 0.0f) damping = 0.0f; // dt が大きすぎて減衰が負になる(＝逆向きに加速する)事故を防ぐ
	knockbackVelocityX_ *= damping;

	// ---- 壁ジャンプで壁と反対方向へ与えた水平速度(時間経過で0へ減衰) ----
	// ノックバックとは別枠。減衰が緩い(kWallJumpPushDamping)ので、壁ジャンプ後にしばらく
	// 壁と反対方向へ流れ続ける。壁方向の入力ロック(上)と合わせて「壁から離れる」挙動になる。
	position_.x += wallJumpVelocityX_ * dt;
	float wallJumpDamp = 1.0f - kWallJumpPushDamping * dt;
	if (wallJumpDamp < 0.0f) wallJumpDamp = 0.0f;
	wallJumpVelocityX_ *= wallJumpDamp;

	// ---- 重力・ジャンプ ----
	// 予備動作(しゃがみ込み)が終わったフレームで実際に踏み切る。
	if (windupKind_ == WU_JUMP && windupTimer_ <= 0.0f) {
		windupKind_ = WU_NONE;
		verticalVelocity_ = kJumpSpeed;
		grounded_ = false;
		coyoteTimer_ = 0.0f;   // 踏み切り後は空中ジャンプさせない
		jumpEffectPending_ = true;
	}
	// しゃがみ中はジャンプできない(しゃがみを解除してから)。
	if (jumpTriggered && !isCrouching_ && windupKind_ == WU_NONE) {
		if (grounded_) {
			// 接地ジャンプ: 予備動作(しゃがみ込み)を挟む。
			windupKind_ = WU_JUMP;
			windupTimer_ = kJumpWindup;   // 踏み切りは kJumpWindup 秒後(アニメのコミットに合わせる)
			coyoteTimer_ = 0.0f;
		} else if (coyoteTimer_ > 0.0f) {
			// ---- コヨーテタイム ----
			// 足場を離れた直後(kCoyoteTime 秒以内)は、空中でも予備動作なしで即ジャンプできる。
			// ジャンプにディレイを入れたぶん、端の踏み外しで損しないための救済。
			verticalVelocity_ = kJumpSpeed;
			coyoteTimer_ = 0.0f;
			jumpEffectPending_ = true;
		} else if (wallContactDir_ != 0) {
			// ---- 壁ジャンプ ----
			// 空中で壁に密着していればジャンプ入力で壁と反対方向へ蹴って跳ぶ。
			// 壁方向へ入力し続けると、跳ねて離れた後に重力を受けながら壁へ近づき直し、
			// 前回より上で再び密着して次の壁ジャンプができる(繰り返すと少しずつ登れる)。
			verticalVelocity_ = kWallJumpUpSpeed;
			wallJumpVelocityX_ = -static_cast<float>(wallContactDir_) * kWallJumpPushXSpeed;
			wallJumpInputLockTimer_ = kWallJumpInputLockTime;
			wallJumpLockDir_ = wallContactDir_; // 蹴った壁の方向への入力を少しの間打ち消す
			wallJumpAnimTimer_ = 0.30f;         // 壁蹴りアニメを再生
			coyoteTimer_ = 0.0f;
		}
	}

	// ---- 壁ずり落ち ----
	// 空中で壁に密着し、その壁方向へ入力していて、かつ足元にブロックが無い(=そのまま落ちる)とき、
	// 落下速度に上限を掛けてゆっくり滑り落ちるようにする。上昇中や壁ジャンプ直後は掛けない。
	if (!grounded_ && !isCrouching_ && wallContactDir_ != 0 && pressingIntoWall &&
		verticalVelocity_ < 0.0f && !HasSolidBelow(kWallSlideGroundProbe)) {
		wallSliding_ = true;
	}

	verticalVelocity_ += kGravity * dt; // 重力を毎フレーム加速度として積分
	if (wallSliding_ && verticalVelocity_ < -kWallSlideMaxFallSpeed) {
		verticalVelocity_ = -kWallSlideMaxFallSpeed; // 壁ずり落ち中は落下速度を頭打ちにする
	}
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
			knockbackVelocityX_ = 0.0f;  // 壁にめり込むノックバックはそこで止める
			wallJumpVelocityX_ = 0.0f;   // 壁ジャンプ直後にすぐ別の壁(通路)へ当たったらそこで止める
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

	// ---- コヨーテタイム ----
	// 接地している間は満タンに補充し、空中では減らす。次フレームのジャンプ判定で
	// 「grounded_ || coyoteTimer_ > 0」として使う(壁蹴り/爆風上昇時は上でその都度 0 にしている)。
	if (grounded_) {
		coyoteTimer_ = kCoyoteTime;
	} else if (coyoteTimer_ > 0.0f) {
		coyoteTimer_ -= dt;
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
	// 近接攻撃も予備動作(引き)を挟む。トリガー時にヒットボックスの雛形を作って貯めておき、
	// kMeleeWindup 秒後(アニメの打撃フレーム)に、その時点の位置で当たり判定を成立させる。
	if (windupKind_ == WU_MELEE && windupTimer_ <= 0.0f) {
		windupKind_ = WU_NONE;
		windupHitbox_.center = {
			position_.x + windupHitOffset_.x,
			position_.y + windupHitOffset_.y,
			position_.z + windupHitOffset_.z };
		windupHitbox_.knockbackDirX = (windupAimX_ >= 0.0f) ? 1.0f : -1.0f;
		hasPendingAttack_ = true;
		pendingAttack_ = windupHitbox_;
	}
	AttackHitbox meleeHitbox;
	if (windupKind_ == WU_NONE &&
		equippedWeapon_->TryMeleeAttack(dt, attackTriggered, position_, aimDirX_, aimDirY_, meleeHitbox)) {
		windupKind_ = WU_MELEE;
		windupTimer_ = kMeleeWindup;
		windupHitbox_ = meleeHitbox;
		windupAimX_ = aimDirX_;
		windupAimY_ = aimDirY_;
		windupHitOffset_ = {
			meleeHitbox.center.x - position_.x,
			meleeHitbox.center.y - position_.y,
			meleeHitbox.center.z - position_.z };
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
	// 命中時のダメージ・ノックバックは武器の種類に関わらず kThrow* の固定値を使う
	// (「弾切れの銃を投げても同じ威力」という仕様。Weapon.h 側の反動とは無関係の別パラメータ)。
	// ただし武器の実体(残弾を含む)は捨てずに pendingThrowWeapon_ で持ち運ぶ ── 着弾しても
	// 残弾が残っていればその場に落ちて拾い直せる(GameScene::UpdateFlyingObjects 側の判断)。
	// 投げも予備動作(振りかぶり)を挟む。トリガーで武器を手放して振りかぶり開始、
	// kThrowWindup 秒後(アニメのリリースフレーム)にその時点の位置・トリガー時の照準で放つ。
	if (throwTriggered && equippedWeapon_->CanBeThrown() && windupKind_ == WU_NONE) {
		windupKind_ = WU_THROW;
		windupTimer_ = kThrowWindup;
		windupAimX_ = aimDirX_;
		windupAimY_ = aimDirY_;
		// 武器の所有権を先に pendingThrowWeapon_ へ移す(振りかぶり中は手に握ったまま描画する)。
		pendingThrowWeapon_ = std::move(equippedWeapon_);
		equippedWeapon_ = std::make_unique<UnarmedWeapon>();
	}
	if (windupKind_ == WU_THROW && windupTimer_ <= 0.0f) {
		windupKind_ = WU_NONE;
		const float ax = windupAimX_;
		const float ay = windupAimY_;
		// 投げる位置は自分の中心から照準方向へ少し離す(自分自身に当たらないようにするため)。
		pendingThrow_.origin = { position_.x + ax * kThrowForwardOffset, position_.y + ay * kThrowForwardOffset, position_.z };
		// 初速は照準方向 × 投擲速度。この後は ArcingProjectile 側が重力を積分して放物線を描く
		// (銃弾と全く同じ物理。Weapon/ArcingProjectile.h 参照)。
		pendingThrow_.velocityX = ax * kThrowSpeed;
		pendingThrow_.velocityY = ay * kThrowSpeed;
		pendingThrow_.gravityScale = kThrowGravityScale;
		pendingThrow_.radius = kThrowRadius;
		pendingThrow_.lifeTime = kThrowLifeTime;         // 何にも当たらなければこの秒数で消える
		pendingThrow_.damage = kThrowDamage;             // 命中時のダメージ(投げ武器固定値)
		pendingThrow_.knockbackPower = kThrowKnockbackPower;
		// 壁に当たっても即座に死なず滑るように弾かれ、実際に足場のある床でMoveAabb基準の
		// 正確な位置に着地して初めて静止する(floorRestitution=0)。これにより、ブロック側面に
		// 当たった投げ武器が地形へめり込んでから不定方向へ解決される(=側面に当てたのに
		// ブロックの上へワープする)不具合を避けられる(ArcingProjectile::Update の bounces_
		// 経路を参照)。
		pendingThrow_.bounces = true;
		pendingThrow_.wallRestitution = kThrowWallRestitution;
		pendingThrow_.floorRestitution = 0.0f;
		// pendingThrowWeapon_ は既に振りかぶり開始時に移譲済み。GameScene が ConsumePendingThrow()
		// で回収し、実体(ArcingProjectile)を生成する。着弾後に残弾が残っていれば地面に落ちる。
		hasPendingThrow_ = true;
	}

	// ---- 状態異常(氷銃・炎銃。ApplySlow/ApplyBurn 参照) ----
	if (slowTimer_ > 0.0f) {
		slowTimer_ -= dt;
		if (slowTimer_ <= 0.0f) {
			slowMultiplier_ = 1.0f; // 効果切れ。通常速度へ戻す
		}
	}
	if (burnTimer_ > 0.0f) {
		burnTimer_ -= dt;
		ApplyDamage(burnDps_ * dt); // 既存のApplyDamageをそのまま使う(ダメージフラッシュも自然に付く)
		if (burnTimer_ <= 0.0f) {
			burnDps_ = 0.0f;
		}
	}

	// ---- ダメージフラッシュ(ApplyDamage で damageFlashTimer_ がセットされている間、赤くする) ----
	if (damageFlashTimer_ > 0.0f) {
		damageFlashTimer_ -= dt;
	}

	// ---- 見た目への反映 ----
	// ダメージ直後は赤・氷結中は水色、それ以外は素の色(アニメモデル=チーム色 / Box=白)。
	// 「当たったのに反応が無い/なぜ動きが重いのか分からない」を防ぐための最小限の演出。
	// 燃焼(burn)は毎フレーム ApplyDamage が呼ばれ続けるので、赤が点滅し続ける形で表現される。
	Vector4 tintColor = animChara_ ? teamColor_ : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	if (damageFlashTimer_ > 0.0f) {
		tintColor = { 1.0f, 0.2f, 0.2f, 1.0f };
	} else if (slowTimer_ > 0.0f) {
		tintColor = { 0.3f, 0.75f, 1.0f, 1.0f };
	}

	if (animChara_) {
		// モデルのローカル原点(足元 Y=0)が position_ の足元へ来るように置く。
		animChara_->SetTranslate({ position_.x, position_.y - kRestHeight, position_.z });
		// 照準の左右で向きを反転する(モデル既定は +X=画面右 向き)。
		// s_modelYawOffsetDeg は Blender→エンジンの向きずれを実機で詰めるための共有調整値。
		const float yaw = ((aimDirX_ >= 0.0f) ? 0.0f : kPi) + DegToRad(s_modelYawOffsetDeg);
		animChara_->SetRotate({ 0.0f, yaw, 0.0f });
		animChara_->SetScale({ kModelScale, kModelScale, kModelScale });
		animChara_->SetMaterialColor(tintColor);
		// しゃがみ専用クリップは未制作(暫定で Idle)。当たり判定カプセルだけは縮む。
		UpdateAnimationState(dt, moveX);
		animChara_->Update(dt);
	} else if (visual_) {
		// フォールバックの Box。しゃがみ中は上から縮めて見た目だけ低くする(足元 y=0 は固定)。
		const float heightScale = isCrouching_ ? kCrouchHeightScale : 1.0f;
		visual_->SetScale({ 0.9f, kRestHeight * 2.0f * heightScale, 0.9f });
		Vector3 visualPos = position_;
		visualPos.y = position_.y - kRestHeight * (1.0f - heightScale);
		visual_->SetTranslate(visualPos);
		visual_->GetMesh().SetColor(tintColor);
		visual_->Update();
	}

	// 手元の武器モデル(持ち替え検出・照準追従)。
	UpdateWeaponModel();

	// 当たり判定カプセルを今の姿勢(立ち/しゃがみ)に合わせる。
	// しゃがんだフレームは、この時点で isCrouching_ が確定している。
	SyncColliderToPose();
}

void Character::Draw() {
	// アニメモデルがある場合は DrawAnimatedModel(Object3D パス)で描くのでここでは何もしない。
	if (!animChara_ && visual_) {
		visual_->Draw();
	}
}

void Character::DispatchAnimatedSkinning(DirectXCore* dxCore) {
	if (animChara_ && dxCore) {
		animChara_->DispatchSkinning(dxCore);
	}
}

void Character::DrawAnimatedModel(DirectXCore* dxCore) {
	if (animChara_ && dxCore) {
		animChara_->Draw(dxCore);
	}
}

void Character::UpdateAnimationState(float dt, float moveX) {
	if (!animChara_) return;

	// ---- このフレームに発生した単発アクションを検出して再生タイマーを立てる ----
	// hasPendingAttack_ / pendingProjectileSpawns_ / hasPendingThrow_ は Update() のこの時点では
	// まだこのフレームの攻撃結果が入っている(GameScene が消費するのは Update() から戻った後)。
	if (hasPendingThrow_) {
		actionAnimClip_ = CLIP_THROW; actionAnimTimer_ = 0.34f;
	} else if (!pendingProjectileSpawns_.empty()) {
		actionAnimClip_ = CLIP_SHOOT; actionAnimTimer_ = 0.18f;   // 連射武器は毎発ここで延長される
	} else if (hasPendingAttack_) {
		actionAnimClip_ = CLIP_PUNCH; actionAnimTimer_ = 0.30f;
	}

	// 着地の瞬間を検出して Land を少しの間だけ優先させる。
	if (grounded_ && !wasGrounded_ && !IsDead()) {
		landTimer_ = 0.16f;
		landEffectPending_ = true;
	}
	if (landTimer_ > 0.0f)     landTimer_ -= dt;
	if (hitAnimTimer_ > 0.0f)  hitAnimTimer_ -= dt;
	if (wallJumpAnimTimer_ > 0.0f) wallJumpAnimTimer_ -= dt;
	if (actionAnimTimer_ > 0.0f)   actionAnimTimer_ -= dt;
	wasGrounded_ = grounded_;

	const bool moving = (moveX > 0.15f || moveX < -0.15f);

	// ---- 優先度順にクリップを選ぶ ----
	int want; bool loop;
	if (IsDead()) {
		want = CLIP_DEATH; loop = false;
	} else if (hitAnimTimer_ > 0.0f) {
		want = CLIP_HIT; loop = false;
	} else if (wallJumpAnimTimer_ > 0.0f) {
		want = CLIP_WALLJUMP; loop = false;
	} else if (windupKind_ == WU_JUMP) {
		want = CLIP_JUMP; loop = false;          // しゃがみ込みの予備動作
	} else if (windupKind_ == WU_THROW) {
		want = CLIP_THROW; loop = false;
	} else if (windupKind_ == WU_MELEE) {
		want = CLIP_PUNCH; loop = false;
	} else if (actionAnimTimer_ > 0.0f && actionAnimClip_ >= 0) {
		want = actionAnimClip_; loop = false;    // リリース後のフォロースルー
	} else if (!grounded_) {
		if (wallSliding_)                 { want = CLIP_WALLSLIDE; loop = true; }
		else if (verticalVelocity_ > 0.1f) { want = CLIP_JUMP; loop = false; }
		else                              { want = CLIP_FALL; loop = true; }
	} else if (landTimer_ > 0.0f) {
		want = CLIP_LAND; loop = false;
	} else if (isCrouching_) {
		want = moving ? CLIP_CROUCHWALK : CLIP_CROUCHIDLE; loop = true;
	} else if (moving) {
		want = CLIP_RUN; loop = true;
	} else {
		want = CLIP_IDLE; loop = true;
	}

	if (want != currentClipIndex_) {
		const std::string path = ClipPath(want);
		if (std::filesystem::exists(path)) {
			currentClipIndex_ = want;
			// ループ系はゆっくり、単発アクションはキビキビ切り替える。
			const bool snappy = (want == CLIP_HIT || want == CLIP_SHOOT || want == CLIP_PUNCH
				|| want == CLIP_THROW || want == CLIP_WALLJUMP || want == CLIP_LAND);
			animChara_->PlayAnimation(path, snappy ? 0.07f : 0.14f);
			animChara_->SetLoop(loop);
		}
	}
}

void Character::UpdateWeaponModel() {
	// 手元に武器を出す位置(自分の中心から照準方向へ少し前、少し上)。
	constexpr float kHandForward = 0.7f; // 照準方向への突き出し
	constexpr float kHandUp = 0.1f;      // 胸〜肩あたりに来るよう少し持ち上げる
	constexpr float kModelScale = 1.0f;

	// 投げの振りかぶり中は、手放し済みだが手に握っている演出として投げる武器を表示し続ける。
	Weapon* shown = (windupKind_ == WU_THROW && pendingThrowWeapon_)
		? pendingThrowWeapon_.get() : equippedWeapon_.get();
	const std::string dir = shown ? shown->GetModelDirectory() : std::string();
	const std::string file = shown ? shown->GetModelFileName() : std::string();

	// 素手・モデル未指定・描画コンテキスト未設定 → モデルは出さない。
	if (dir.empty() || file.empty() || !object3DManager_ || !weaponModelDxCore_) {
		weaponModel_.reset();
		weaponModelKey_.clear();
		return;
	}

	// 前フレームと違う武器を持っていれば作り直す。
	const std::string key = dir + "/" + file;
	if (key != weaponModelKey_) {
		weaponModel_ = std::make_unique<Object3DInstance>();
		weaponModel_->Initialize(object3DManager_, weaponModelDxCore_, dir, file, name_ + "_Weapon");
		weaponModel_->SetCamera(camera_);
		weaponModel_->SetScale({ kModelScale, kModelScale, kModelScale });
		weaponModelKey_ = key;
	}

	// 照準方向へ向ける。
	//
	// 本体(animChara_、500行)と同じく「左右は反転(ミラー)」「上下はモデルを傾ける」の
	// 2段構えにする。素朴に Z 軸だけでフル回転させると、左を狙うたびに上方向が
	// ワールド下方向へ回り込んでモデルが上下逆さまになってしまう(症状として報告されたバグ)。
	//
	// さらに、武器メッシュは cook 時の OBJ→mesh 変換(RH→LH のため頂点X座標を反転。
	// cook_assets.py 参照)の影響で、ローカル -X が銃口方向になっている
	// (generate_weapon_model.py の規約は +X=銃口方向だが、cook でそれが反転される)。
	// そのため本体(500行、+X=画面右向きモデル)とは yaw の 0/π が入れ替わっている。
	const float facingSign = (aimDirX_ >= 0.0f) ? 1.0f : -1.0f;
	const float weaponYaw = (facingSign >= 0.0f) ? kPi : 0.0f;
	// tiltAngle に facingSign を掛けているのは、上の yaw で左右反転した側だと
	// Z回転(上下の傾き)の効き方がミラーで逆になるため。狙った通りに動かない場合は
	// まずこの符号(facingSign * aimDirY_ の掛け方)を疑うこと。
	const float tiltAngle = std::atan2(facingSign * aimDirY_, std::fabs(aimDirX_));
	weaponModel_->SetRotate({ 0.0f, weaponYaw, tiltAngle });
	weaponModel_->SetTranslate({
		position_.x + aimDirX_ * kHandForward,
		position_.y + aimDirY_ * kHandForward + kHandUp,
		position_.z });
	weaponModel_->Update();
}

void Character::DrawWeaponModel(DirectXCore* dxCore) {
	if (weaponModel_) {
		weaponModel_->Draw(dxCore);
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
	// 被弾判定は今の姿勢のカプセルで取る。しゃがみ中は背が低く潰れているので、
	// 立ち姿勢の頭の高さを狙った攻撃はしゃがんでいれば当たらない。
	Vector3 capsuleCenter;
	float capsuleCyl, capsuleRadius;
	GetPoseCapsule(capsuleCenter, capsuleCyl, capsuleRadius);
	const bool hit = CollisionGeometry::TestSphereCapsule(
		hitbox.center, hitbox.radius,
		capsuleCenter, kIdentityAxes, capsuleCyl, capsuleRadius);
	if (!hit) {
		return false;
	}

	hitAnimTimer_ = 0.28f;   // 被弾のけぞりアニメ
	// のけぞりで踏み切り・パンチはキャンセル(投げは武器が既にコミット済みなので継続)。
	if (windupKind_ == WU_JUMP || windupKind_ == WU_MELEE) {
		windupKind_ = WU_NONE;
	}
	ApplyDamage(hitbox.damage);
	// ノックバックは「攻撃した瞬間の攻撃側の向き」をそのまま使う(hitbox.knockbackDirX)。
	// 自分の位置と命中位置(hitbox.center)から向きを逆算すると、密着距離では
	// 攻撃ヒットボックスの中心が自分を追い越してしまい、向きが反転するバグになるため。
	ApplyKnockback(hitbox.knockbackDirX, hitbox.knockbackPower);
	// 状態異常(氷銃・炎銃)。duration<=0 の武器(既存の全武器)は既定値のままなので何も起きない。
	if (hitbox.slowDuration > 0.0f) {
		ApplySlow(hitbox.slowMultiplier, hitbox.slowDuration);
	}
	if (hitbox.burnDuration > 0.0f) {
		ApplyBurn(hitbox.burnDps, hitbox.burnDuration);
	}
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

bool Character::ConsumePendingThrow(ProjectileSpawnRequest& outSpawn, std::unique_ptr<Weapon>& outWeapon) {
	if (!hasPendingThrow_) {
		return false;
	}
	outSpawn = pendingThrow_;
	outWeapon = std::move(pendingThrowWeapon_); // 残弾を保持したまま武器本体の所有権を渡す
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

Vector3 Character::GetColliderHalfExtent() const {
	const float heightScale = isCrouching_ ? kCrouchHeightScale : 1.0f;
	return { kCapsuleRadius, kRestHeight * heightScale, kCapsuleRadius };
}

Vector3 Character::GetColliderCenter() const {
	const float heightScale = isCrouching_ ? kCrouchHeightScale : 1.0f;
	const float centerYOffset = -kRestHeight * (1.0f - heightScale); // しゃがみ中は足元固定で中心が下がる
	return { position_.x, position_.y + centerYOffset, position_.z };
}

void Character::GetPoseCapsule(Vector3& outCenter, float& outCylinderHeight, float& outRadius) const {
	const float heightScale = isCrouching_ ? kCrouchHeightScale : 1.0f;
	outRadius = kCapsuleRadius; // 横幅は姿勢で変えない
	// 総高 = 2 * (立ち姿勢の半高) * heightScale。円柱部分 = 総高 - 両端の半球ぶん。
	float cyl = 2.0f * kRestHeight * heightScale - 2.0f * kCapsuleRadius;
	if (cyl < 0.0f) cyl = 0.0f; // しゃがみ中は球に潰れる
	outCylinderHeight = cyl;
	outCenter = GetColliderCenter();
}

void Character::SyncColliderToPose() {
	Vector3 center;
	float cyl, radius;
	GetPoseCapsule(center, cyl, radius);
	Collider& c = CollisionSystem::GetInstance()->ColliderOf(this);
	c.capsuleHeight = cyl;
	c.capsuleRadius = radius;
	c.offset = { 0.0f, center.y - position_.y, 0.0f }; // 中心 = position_ + offset
}

void Character::ApplyBlastKnockback(float dirX, float dirY, float power) {
	float len = std::sqrt(dirX * dirX + dirY * dirY);
	if (len < 0.0001f) {
		// 爆心とほぼ同じ位置なら真上へ吹き飛ばす(方向が定まらないため)。
		dirX = 0.0f;
		dirY = 1.0f;
		len = 1.0f;
	}
	const float nx = dirX / len;
	const float ny = dirY / len;
	knockbackVelocityX_ = nx * power;   // ApplyKnockback と同じ上書き式
	verticalVelocity_ = ny * power;
	coyoteTimer_ = 0.0f;               // 爆風で吹き飛んだ直後にコヨーテジャンプさせない
	if (ny > 0.0f) {
		grounded_ = false; // 上向きに飛ぶなら空中扱いにして弧を描かせる
	}
}

void Character::ApplySlow(float multiplier, float duration) {
	if (multiplier >= 1.0f || duration <= 0.0f) {
		return; // 減速にならない/一瞬も持続しない指定は無視する
	}
	// ApplyKnockback と同じ上書き式。再命中すればその時点の強さ・持続時間にリセットされる。
	slowMultiplier_ = multiplier;
	slowTimer_ = duration;
}

void Character::ApplyBurn(float dps, float duration) {
	if (dps <= 0.0f || duration <= 0.0f) {
		return;
	}
	burnDps_ = dps;
	burnTimer_ = duration;
}

void Character::ResetForNewRound(const Vector3& spawnPos) {
	// HP・速度・しゃがみ/接地状態・攻撃クールダウンをすべて初期状態に戻し、spawnPos へ再配置する。
	// GameScene::CheckKnockoutAndReset(得点直後のその場リセット)と GameScene::LoadStage
	// (ランダムなステージへの丸ごと切替)の両方から呼ばれる。
	hp_ = kMaxHP;
	position_ = spawnPos;
	knockbackVelocityX_ = 0.0f;
	verticalVelocity_ = 0.0f;
	wallJumpVelocityX_ = 0.0f;
	wallJumpInputLockTimer_ = 0.0f;
	wallJumpLockDir_ = 0;
	wallContactDir_ = 0;
	wallSliding_ = false;
	grounded_ = true;
	coyoteTimer_ = 0.0f;
	isCrouching_ = false;
	SyncColliderToPose(); // 立ち姿勢のカプセルへ戻す
	hasPendingAttack_ = false;
	pendingProjectileSpawns_.clear();
	hasPendingThrow_ = false;
	pendingThrowWeapon_.reset(); // 消費されなかった投げ武器が万一残っていても、ここで確実に手放す
	equippedWeapon_ = std::make_unique<UnarmedWeapon>(); // 前ラウンドの武器を次ラウンドへ持ち越さない(素手に戻す)
	damageFlashTimer_ = 0.0f;
	slowMultiplier_ = 1.0f;
	slowTimer_ = 0.0f;
	burnDps_ = 0.0f;
	burnTimer_ = 0.0f;

	if (visual_) {
		visual_->SetTranslate(position_);
		visual_->GetMesh().SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // 赤フラッシュが残ったまま次ラウンドへ持ち越さない
	}
	// アニメ状態も初期化(赤フラッシュ・死亡ポーズを次ラウンドへ持ち越さない)。
	wasGrounded_ = true;
	landTimer_ = 0.0f;
	jumpEffectPending_ = false;
	landEffectPending_ = false;
	hitAnimTimer_ = 0.0f;
	wallJumpAnimTimer_ = 0.0f;
	actionAnimTimer_ = 0.0f;
	actionAnimClip_ = -1;
	windupKind_ = WU_NONE;
	windupTimer_ = 0.0f;
	if (animChara_) {
		animChara_->SetMaterialColor(teamColor_);
		currentClipIndex_ = CLIP_IDLE;
		if (std::filesystem::exists(ClipPath(CLIP_IDLE))) {
			animChara_->PlayAnimation(ClipPath(CLIP_IDLE), 0.0f);
			animChara_->SetLoop(true);
		}
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
	if (animChara_) {
		ImGui::Separator();
		const int c = currentClipIndex_;
		ImGui::Text("Anim clip: %s", (c >= 0 && c < CLIP_COUNT) ? kClipNames[c] : "(none)");
		// 全キャラ共有。Blender→エンジンの向きずれをここで詰める。
		ImGui::SliderFloat("Model Yaw Offset (shared)", &s_modelYawOffsetDeg, -180.0f, 180.0f);
	}
#endif
}
