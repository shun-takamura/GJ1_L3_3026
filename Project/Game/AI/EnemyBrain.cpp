#include "AI/EnemyBrain.h"

#include <cmath>

#include "AI/AINavigation.h"
#include "AI/PlayerModel.h"
#include "Character/Character.h"

void EnemyBrain::Initialize(bool turretMode) {
	turretMode_ = turretMode;
	ResetForNewRound();
}

void EnemyBrain::ResetForNewRound() {
	state_ = turretMode_ ? State::Attack : State::Idle;
	reactionTimer_ = 0.0f;
	attackRefireTimer_ = 0.0f;
	stateTimer_ = 0.0f;
	jumpCooldown_ = 0.0f;
}

void EnemyBrain::TransitionTo(State next) {
	if (state_ == next) {
		return;
	}
	state_ = next;
	stateTimer_ = 0.0f;
}

const char* EnemyBrain::GetStateName() const {
	switch (state_) {
		case State::Idle:     return "Idle";
		case State::Approach: return "Approach";
		case State::Attack:   return "Attack";
		case State::Retreat:  return "Retreat";
	}
	return "?";
}

CharacterInput EnemyBrain::Think(const BrainContext& ctx) {
	CharacterInput out;

	if (!ctx.self || !ctx.target || ctx.self->IsDead()) {
		return out; // 何も入力しない（棒立ち）
	}

	const float dt = ctx.dt;
	stateTimer_ += dt;
	if (reactionTimer_ > 0.0f)     reactionTimer_ -= dt;
	if (attackRefireTimer_ > 0.0f) attackRefireTimer_ -= dt;
	if (jumpCooldown_ > 0.0f)      jumpCooldown_ -= dt;

	// ---- 索敵 ----
	PerceptionInput pin;
	pin.selfPos = ctx.self->GetPosition();
	pin.targetPos = ctx.target->GetPosition();
	pin.selfHp = ctx.self->GetHP();
	pin.selfMaxHp = ctx.self->GetMaxHP();
	pin.targetIsDead = ctx.target->IsDead();
	pin.stage = ctx.stage;
	const PerceptionResult p = perception_.Sense(pin);

	// ---- 照準は常に相手へ ----
	// TODO(Day5): PlayerModel の AimErrorRad() ぶん回転させ、発砲後の移動癖から
	//   置き撃ち（相手の少し先を狙う）を入れる。
	out.aimDirX = p.dirToTargetX;
	out.aimDirY = p.dirToTargetY;

	float reactionDelay = kBaseReactionDelay;
	if (ctx.playerModel) {
		reactionDelay = ctx.playerModel->ReactionDelay();
	}

	// 素手なら近接間合いを、武器持ちなら射撃間合いを目指す。
	const bool wantMelee = ctx.self->CanPickUpWeapon();
	const float engageRange = wantMelee ? kMeleeRange : kEngageRanged;

	// ================= 撤退ライン: その場で撃つだけの的 =================
	if (turretMode_) {
		state_ = State::Attack;
		const bool canFire = p.hasLineOfSight && !pin.targetIsDead;
		if (canFire && reactionTimer_ <= 0.0f) {
			out.attackHeld = true;
			if (attackRefireTimer_ <= 0.0f) {
				out.attackTriggered = true;
				attackRefireTimer_ = kAttackRefire;
			}
		} else if (!canFire) {
			reactionTimer_ = reactionDelay;
		}
		return out;
	}

	// ================= 状態遷移 =================
	switch (state_) {
		case State::Idle:
			if (!pin.targetIsDead) {
				TransitionTo(State::Approach);
			}
			break;

		case State::Approach:
			if (p.selfLowHp && p.distance < kRetreatKeepOut) {
				TransitionTo(State::Retreat);
			} else if (p.distance <= engageRange && p.hasLineOfSight) {
				TransitionTo(State::Attack);
				reactionTimer_ = reactionDelay;
			}
			break;

		case State::Attack:
			if (p.selfLowHp) {
				TransitionTo(State::Retreat);
			} else if (!p.hasLineOfSight || p.distance > engageRange * 1.35f) {
				TransitionTo(State::Approach);
			}
			break;

		case State::Retreat:
			// 少し距離を取れた／HP 水準が戻った（＝ラウンドリセット）なら再び攻める。
			if (stateTimer_ > 2.5f && (p.distance > kRetreatKeepOut || !p.selfLowHp)) {
				TransitionTo(State::Approach);
			}
			break;
	}

	// ================= 状態ごとの移動意図 =================
	const float towardX = (p.dirToTargetX >= 0.0f) ? 1.0f : -1.0f;
	float desiredMoveX = 0.0f;
	switch (state_) {
		case State::Approach:
			desiredMoveX = towardX;
			break;
		case State::Retreat:
			desiredMoveX = -towardX;
			break;
		case State::Attack:
			if (wantMelee && p.distance > kMeleeRange * 0.8f) {
				desiredMoveX = towardX;            // 密着を維持
			} else if (!wantMelee && p.distance < kTooClose) {
				desiredMoveX = -towardX;           // 銃なのに近すぎるので少し引く
			}
			break;
		default:
			break;
	}

	// ================= ナビ: 穴・壁・場外の先読み =================
	if (ctx.stage) {
		if (desiredMoveX != 0.0f) {
			const AINav::MoveHazard hz = AINav::Probe(*ctx.stage, pin.selfPos, desiredMoveX,
				kFeetHalfY, kLookAhead);
			if (hz.edgeAhead) {
				desiredMoveX = 0.0f; // 場外へは歩かない（追い詰められても自滅しない）
			} else if ((hz.pitAhead || hz.wallAhead) && jumpCooldown_ <= 0.0f && !p.targetBelow) {
				// 穴は飛び越す／壁は乗り越えを試みる。
				// 相手が下にいるなら穴は「落ちて追う」ルートなので飛ばない。
				out.jumpTriggered = true;
				jumpCooldown_ = kJumpInterval;
			}
		}
		// 相手が上にいて近い → ジャンプで段差を登る。
		if (p.targetAbove && p.horizontalGap < 2.5f && jumpCooldown_ <= 0.0f) {
			out.jumpTriggered = true;
			jumpCooldown_ = kJumpInterval;
		}
	}
	out.moveX = desiredMoveX;

	// ================= 攻撃入力 =================
	const float attackReach = wantMelee ? kMeleeRange * 1.2f : kRangedMax;
	const bool canAttackNow = (state_ == State::Attack) && p.hasLineOfSight
		&& !pin.targetIsDead && (p.distance <= attackReach);
	if (canAttackNow) {
		if (reactionTimer_ <= 0.0f) {
			out.attackHeld = true; // 連射武器はこれだけで撃ち続ける
			if (attackRefireTimer_ <= 0.0f) {
				out.attackTriggered = true; // 単発武器・素手の取りこぼし防止
				attackRefireTimer_ = kAttackRefire;
			}
		}
	} else {
		// 撃てる状況が切れたら、次に撃てるようになったとき再び反応遅延を課す。
		reactionTimer_ = reactionDelay;
	}

	return out;
}
