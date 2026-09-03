#include "AI/EnemyBrain.h"

#include <algorithm>
#include <cmath>

#include "AI/AINavigation.h"
#include "AI/PlayerModel.h"
#include "Character/Character.h"
#include "Common/IStageQuery.h"
#include "RandomGenerator.h"

namespace {
	float Clamp(float v, float lo, float hi) {
		if (v < lo) return lo;
		if (v > hi) return hi;
		return v;
	}
}

void EnemyBrain::Initialize(bool turretMode) {
	turretMode_ = turretMode;
	selfFallCount_ = 0; // 試合開始でリセット（ラウンド跨ぎでは保持）
	ResetForNewRound();
}

void EnemyBrain::NotifyDeath(bool wasOutOfBounds) {
	if (wasOutOfBounds) {
		selfFallCount_ = (std::min)(selfFallCount_ + 1, kMaxFallCaution);
	}
}

void EnemyBrain::ResetForNewRound() {
	state_ = turretMode_ ? State::Attack : State::Idle;
	reactionTimer_ = 0.0f;
	attackRefireTimer_ = 0.0f;
	stateTimer_ = 0.0f;
	jumpCooldown_ = 0.0f;

	prevTargetValid_ = false;
	prevTargetPos_ = {};
	targetVelX_ = 0.0f;

	aimErrorCurr_ = 0.0f;
	aimErrorTimer_ = 0.0f;

	losLostTimer_ = 0.0f;
	prevSelfValid_ = false;
	prevSelfX_ = 0.0f;
	stuckTimer_ = 0.0f;
	reverseTimer_ = 0.0f;

	throwCdTimer_ = 0.0f;

	moveCommitTimer_ = 0.0f;
	committedMoveDir_ = 0.0f;

	fetchBestDist_ = 0.0f;
	fetchStallTimer_ = 0.0f;
	pickupBlacklistTimer_ = 0.0f;
	pickupBlacklistX_ = 0.0f;
	terrainBlockedTimer_ = 0.0f;
	frozenTimer_ = 0.0f;
	corneredHoldTimer_ = 0.0f;
	dodgeLatched_ = false;
	dodgeRoll_ = false;
	strafePhase_ = 0.0f;
}

void EnemyBrain::TransitionTo(State next) {
	if (state_ == next) {
		return;
	}
	state_ = next;
	stateTimer_ = 0.0f;
	losLostTimer_ = 0.0f;
}

const char* EnemyBrain::GetStateName() const {
	switch (state_) {
		case State::Idle:        return "Idle";
		case State::Approach:    return "Approach";
		case State::Attack:      return "Attack";
		case State::Retreat:     return "Retreat";
		case State::FetchWeapon: return "FetchWeapon";
	}
	return "?";
}

CharacterInput EnemyBrain::Think(const BrainContext& ctx) {
	CharacterInput out;

	if (!ctx.self || !ctx.target || ctx.self->IsDead()) {
		prevTargetValid_ = false;
		prevSelfValid_ = false;
		return out; // 何も入力しない（棒立ち）
	}

	const float dt = ctx.dt;
	stateTimer_ += dt;
	if (reactionTimer_ > 0.0f)     reactionTimer_ -= dt;
	if (attackRefireTimer_ > 0.0f) attackRefireTimer_ -= dt;
	if (jumpCooldown_ > 0.0f)      jumpCooldown_ -= dt;
	if (throwCdTimer_ > 0.0f)      throwCdTimer_ -= dt;
	if (corneredHoldTimer_ > 0.0f) corneredHoldTimer_ -= dt;

	// ---- 索敵 ----
	PerceptionInput pin;
	pin.selfPos = ctx.self->GetPosition();
	pin.targetPos = ctx.target->GetPosition();
	pin.selfHp = ctx.self->GetHP();
	pin.selfMaxHp = ctx.self->GetMaxHP();
	pin.targetIsDead = ctx.target->IsDead();
	pin.stage = ctx.stage;
	const PerceptionResult p = perception_.Sense(pin);

	// ---- ターゲットの水平速度を推定（置き撃ち用。急変を均すため補間） ----
	if (prevTargetValid_ && dt > 1e-4f) {
		const float rawVx = (pin.targetPos.x - prevTargetPos_.x) / dt;
		targetVelX_ += (rawVx - targetVelX_) * 0.3f;
	}
	prevTargetPos_ = pin.targetPos;
	prevTargetValid_ = true;

	// ---- 学習パラメータ ----
	float reactionDelay = kBaseReactionDelay;
	bool reckless = false;
	float aimErrorAmp = 0.0f;
	float crouchRatio = 0.0f;
	bool campingBreakable = false;
	float leadFactor = 0.5f;
	float dodgeSkill = 0.0f;
	float spacingSkill = 0.0f;
	int pushDir = 0;
	bool prefersClose = false;
	bool prefersRanged = false;
	if (ctx.playerModel) {
		reactionDelay = ctx.playerModel->ReactionDelay();
		reckless = (ctx.playerModel->Tier() < kRecklessUntilTier);
		aimErrorAmp = ctx.playerModel->AimErrorRad();
		crouchRatio = ctx.playerModel->CrouchRatio();
		campingBreakable = ctx.playerModel->LikesCampingBreakable();
		leadFactor = ctx.playerModel->LeadFactor();
		dodgeSkill = ctx.playerModel->DodgeSkill();
		spacingSkill = ctx.playerModel->SpacingSkill();
		pushDir = ctx.playerModel->PreferredPushDir();
		prefersClose = ctx.playerModel->PrefersCloseCombat();
		prefersRanged = ctx.playerModel->PrefersRangedKeepaway();
	}
	// 自滅を重ねるほど「跳べる」と判断する幅を狭め、崖の検知距離を広げる（穴への学習）。
	const float cautionFactor = 1.0f + 0.6f * static_cast<float>(selfFallCount_);
	float jumpGap = (reckless ? kAiMaxJumpGap * kRecklessJumpGapMul : kAiMaxJumpGap) / cautionFactor;
	const float edgeCaution = kEdgeCaution * cautionFactor;

	const bool wantMelee = ctx.self->CanPickUpWeapon(); // 素手 = 近接間合いを目指す
	const float engageRange = wantMelee ? kMeleeRange : kEngageRanged;

	// 残弾。-1 = 弾の概念なし(素手)。0 = 弾切れ(＝撃てない銃を持たされている)。
	const int  ammo = ctx.self->GetEquippedAmmo();
	const bool hasWeapon = !wantMelee;
	const bool outOfAmmo = (ammo == 0);
	const bool lowAmmo = (ammo > 0 && ammo <= kLowAmmoCount);
	// 弾切れの銃は持っていても無意味。手ぶら同然として扱う（拾い直しに行かせる）。
	const bool effectivelyUnarmed = wantMelee || outOfAmmo;

	const bool crouchCounter = (crouchRatio > kCrouchCounterRatio);

	// ---- Day5 の特殊エイムモードを先に決める ----
	// 壊れ床落とし: 相手が危うい壊れ床に居座っているなら、本体ではなく足場を狙う。
	const bool floorDrop = campingBreakable && !wantMelee && ctx.stage
		&& ctx.stage->IsPrecariousBreakableFloor(pin.targetPos);

	// 武器を投げつける条件:
	//   ・弾切れ → 無条件で投げる（撃てない銃を持ち続ける意味がない＝手を空けて拾い直す）
	//   ・しゃがみ多用の相手が今しゃがんでいる → 山なりに投げて崩す（拾い直せる pickup がある時のみ）
	const bool wantThrowWeapon = hasWeapon && throwCdTimer_ <= 0.0f && (
		outOfAmmo
		|| (crouchCounter && ctx.target->IsCrouching()
			&& p.distance > kMeleeRange * 2.0f && p.distance < kThrowRange
			&& p.hasLineOfSight && ctx.nearestPickup != nullptr));

	// ---- エイム誤差を一定間隔で引き直す ----
	aimErrorTimer_ -= dt;
	if (aimErrorTimer_ <= 0.0f) {
		aimErrorCurr_ = RandomGenerator::Instance().NextFloat(-aimErrorAmp, aimErrorAmp);
		aimErrorTimer_ = kAimErrorRefresh;
	}

	// ---- 照準点を組み立てる ----
	float aimX = pin.targetPos.x;
	float aimY = pin.targetPos.y;
	if (floorDrop) {
		aimY = pin.targetPos.y - kFloorAimDrop; // 足場のセルへ
	} else {
		if (!wantMelee) {
			const float leadTime = Clamp(p.distance / kBulletSpeedGuess, 0.0f, kMaxLeadTime);
			aimX += targetVelX_ * leadTime * leadFactor; // 進行方向へ置き撃ち（序盤はリードが甘い）
		}
		if (crouchCounter) {
			aimY -= 0.5f;                         // 足元寄りを狙う
		}
		if (wantThrowWeapon) {
			aimY += 1.5f;                         // 山なりに投げるため少し上へ
		}
	}
	float dirX = aimX - pin.selfPos.x;
	float dirY = aimY - pin.selfPos.y;
	{
		const float len = std::sqrt(dirX * dirX + dirY * dirY);
		if (len > 1e-4f) { dirX /= len; dirY /= len; }
		else { dirX = p.dirToTargetX; dirY = p.dirToTargetY; }
	}
	// エイム誤差ぶん回転させる。
	{
		const float c = std::cos(aimErrorCurr_);
		const float s = std::sin(aimErrorCurr_);
		const float rx = dirX * c - dirY * s;
		const float ry = dirX * s + dirY * c;
		dirX = rx; dirY = ry;
	}
	out.aimDirX = dirX;
	out.aimDirY = dirY;

	// ================= 撤退ライン: その場で撃つだけの的 =================
	if (turretMode_) {
		state_ = State::Attack;
		const bool canFire = (p.hasLineOfSight || floorDrop) && !pin.targetIsDead;
		if (canFire && reactionTimer_ <= 0.0f) {
			out.attackHeld = true;
			if (attackRefireTimer_ <= 0.0f) {
				out.attackTriggered = true;
				attackRefireTimer_ = kAttackRefire;
			}
		} else if (!canFire) {
			reactionTimer_ = reactionDelay;
		}
		prevSelfX_ = pin.selfPos.x;
		prevSelfValid_ = true;
		return out;
	}

	// ---- 射線ロスト計測（Attack 中だけ） ----
	if (state_ == State::Attack && !p.hasLineOfSight && !floorDrop) {
		losLostTimer_ += dt;
	} else {
		losLostTimer_ = 0.0f;
	}

	// ---- 武器を拾いに行くべきか ----
	float pickupDist = 0.0f;
	if (ctx.nearestPickup) {
		const float px = ctx.nearestPickup->x - pin.selfPos.x;
		const float py = ctx.nearestPickup->y - pin.selfPos.y;
		pickupDist = std::sqrt(px * px + py * py);
	}
	// 直前に「届かない」と判断した pickup 付近は、しばらく無視する。
	if (pickupBlacklistTimer_ > 0.0f) {
		pickupBlacklistTimer_ -= dt;
	}
	const bool pickupBlacklisted = ctx.nearestPickup && pickupBlacklistTimer_ > 0.0f
		&& std::fabs(ctx.nearestPickup->x - pickupBlacklistX_) < 2.5f;

	const bool wantWeaponFetch = ctx.nearestPickup && !pickupBlacklisted
		&& pickupDist < kFetchMaxDist && (
		// 手ぶら／弾切れ: 殴られる距離でない限り取りに行く。
		(effectivelyUnarmed && p.distance > kFetchSafeDist)
		// 残弾わずか: 相手が遠く、武器のほうが近い（安全に取れる）ときだけ先回りで拾う。
		|| (lowAmmo && p.distance > kEngageRanged && pickupDist < p.distance));

	// 手ぶら／弾切れで拾える武器があるなら、他のどの行動よりも優先して取りに行く。
	// （これを最上位に置かないと、崖越しの相手を撃とうとする terrainBlocked 復帰と
	//   Attack↔Approach で 1 フレーム内ループし、FetchWeapon の判定に到達できない。）
	if (wantWeaponFetch && state_ != State::FetchWeapon && state_ != State::Retreat) {
		TransitionTo(State::FetchWeapon);
	}

	// ================= 状態遷移 =================
	switch (state_) {
		case State::Idle:
			if (!pin.targetIsDead) {
				TransitionTo(wantWeaponFetch ? State::FetchWeapon : State::Approach);
			}
			break;

		case State::Approach:
			// 瀕死でも、相手が近接圏に踏み込んで来たときだけ引く（遠ければ撃ち続ける）。
			if (p.selfLowHp && p.distance < kMeleeRange * 2.5f && corneredHoldTimer_ <= 0.0f) {
				TransitionTo(State::Retreat);
			} else if (wantWeaponFetch) {
				TransitionTo(State::FetchWeapon);
			} else if (p.distance <= engageRange && p.hasLineOfSight) {
				TransitionTo(State::Attack);
				reactionTimer_ = reactionDelay;
			}
			break;

		case State::Attack: {
			const float breakDist = wantMelee ? kMeleeRange * 2.5f : kRangedMax * 1.1f;
			if (p.selfLowHp && p.distance < kMeleeRange * 2.0f && corneredHoldTimer_ <= 0.0f) {
				TransitionTo(State::Retreat);
			} else if (losLostTimer_ > kLoSLostTimeout) {
				TransitionTo(State::Approach);
			} else if (!floorDrop && (!p.hasLineOfSight || p.distance > breakDist)) {
				TransitionTo(State::Approach);
			}
			break;
		}

		case State::Retreat:
			if (stateTimer_ > kRetreatDuration && (p.distance > kRetreatKeepOut || !p.selfLowHp)) {
				TransitionTo(State::Approach);
			}
			break;

		case State::FetchWeapon:
			if (p.distance < kFetchSafeDist && p.hasLineOfSight) {
				TransitionTo(State::Approach);          // 目の前に来られた、戦うしかない
			} else if (stateTimer_ > kFetchCommitTime && !wantWeaponFetch) {
				// 一度取りに行くと決めたら kFetchCommitTime は迷わない（往復・プルプル防止）。
				TransitionTo(State::Approach);
			}
			break;
	}

	// ---- FetchWeapon の行き詰まり検知 ----
	// 距離が縮まらないまま一定時間経ったら、その pickup は（壁・段差で）届かないと判断し、
	// 一時的に無視して戦闘へ戻る（届かない武器を追ってジャンプし続けるループを断つ）。
	if (state_ == State::FetchWeapon && ctx.nearestPickup) {
		if (stateTimer_ < 0.05f || pickupDist < fetchBestDist_ - 0.5f) {
			// 「はっきり近づけた」ときだけ停滞タイマーをリセット（微振動では戻さない）。
			fetchBestDist_ = pickupDist;
			fetchStallTimer_ = 0.0f;
		} else {
			fetchStallTimer_ += dt;
		}
		if (fetchStallTimer_ > 2.0f) {
			pickupBlacklistX_ = ctx.nearestPickup->x;
			pickupBlacklistTimer_ = 8.0f;
			fetchStallTimer_ = 0.0f;
			TransitionTo(State::Approach);
		}
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
		case State::FetchWeapon:
			if (ctx.nearestPickup) {
				const float pdx = ctx.nearestPickup->x - pin.selfPos.x;
				const float pdy = ctx.nearestPickup->y - pin.selfPos.y;
				if (std::fabs(pdx) < 0.8f) {
					// ほぼ真上/真下 → 左右に振らない（符号反転の往復を防ぐ）。
					if (pdy > 0.5f) {
						// 上にある → ゆっくり寄りながらジャンプで段差を上る。
						desiredMoveX = (pdx >= 0.0f ? 1.0f : -1.0f) * 0.35f;
						if (jumpCooldown_ <= 0.0f) {
							out.jumpTriggered = true;
							jumpCooldown_ = kJumpInterval;
						}
					} else {
						desiredMoveX = 0.0f; // もうほぼ乗っている（GameScene が拾わせる）
					}
				} else {
					desiredMoveX = (pdx >= 0.0f) ? 1.0f : -1.0f;
				}
			}
			break;
		case State::Attack:
			if (wantMelee) {
				if (p.distance > kMeleeRange * 0.8f) {
					desiredMoveX = towardX;                   // 密着維持
				}
			} else {
				// 学習: 近接で倒せる相手なら詰める / 遠距離で倒せる相手なら距離を保つ。
				const float shootDist = prefersClose ? kMeleeRange * 2.0f
					: (prefersRanged ? kPreferredShootDist + 3.0f : kPreferredShootDist);
				const float backOff = prefersRanged ? kTooClose + 2.5f : kTooClose;
				if (p.distance < backOff) {
					desiredMoveX = -towardX;                  // 近すぎるので引く
				} else if (p.distance > shootDist
					|| (crouchCounter && p.distance > kMeleeRange * 1.5f)) {
					desiredMoveX = towardX;                   // 撃ちながら間合いを詰める
				}
				// 学習: プレイヤーを落としやすい向きへ押せる位置に回り込む。
				//   弾のノックバックは「自分→相手」の向き。相手を pushDir へ飛ばすには
				//   自分が相手の (-pushDir) 側にいればよい。逆側なら通り抜けを試みる。
				if (pushDir != 0 && desiredMoveX == 0.0f) {
					const float sideOfTarget = (pin.targetPos.x - pin.selfPos.x); // >0: 自分は相手の左
					if (sideOfTarget * static_cast<float>(pushDir) < 0.0f) {
						desiredMoveX = towardX; // 反対側にいる → 相手を越えて回り込む
					}
				}
			}
			break;
		default:
			break;
	}

	// ================= 崖から離れるポジション取り =================
	// 自分の左右 kEdgeCaution 以内に「落ちる場所」があれば、そちら向きの移動を打ち消し、
	// 退避・交戦中は安全側へ寄る。ステージから落ちにくい立ち回りにするための補正。
	float edgeBias = 0.0f;
	bool edgeHeld = false; // 崖回避で意図的に足を止めた（＝詰まりではない）
	if (ctx.stage) {
		auto hazardSide = [&](float dir) -> bool {
			const AINav::MoveHazard h = AINav::Probe(*ctx.stage, pin.selfPos, dir,
				kFeetHalfY, edgeCaution, jumpGap, kAiMaxJumpUp, kMaxSafeDrop);
			// 「飛び降りて着地できる段差(dropAhead)」は崖扱いしない。
			return h.edgeAhead || (h.pitAhead && !h.jumpClears && !h.dropAhead);
		};
		const bool hazL = hazardSide(-1.0f);
		const bool hazR = hazardSide(1.0f);
		if (hazL && !hazR)      edgeBias = 1.0f;
		else if (hazR && !hazL) edgeBias = -1.0f;

		if (edgeBias != 0.0f && desiredMoveX * edgeBias < 0.0f) {
			// 移動意図が崖側を向いている。
			if (state_ == State::Retreat) {
				// 逃げる方向が崖＝もう下がれない → 逃げるのをやめて反撃に転じる。
				TransitionTo(State::Attack);
				corneredHoldTimer_ = 2.5f; // しばらく Retreat へ戻らない（往復防止）
				desiredMoveX = 0.0f;
				edgeHeld = true;
			} else {
				// 「相手へ詰める」意図なら止めない（ナビの hardStop が崖ぎわで止める）。
				const bool closingOnTarget =
					(state_ == State::Approach || state_ == State::Attack)
					&& (desiredMoveX * towardX > 0.0f);
				if (!closingOnTarget) {
					desiredMoveX = 0.0f;
					edgeHeld = true;
					if (!wantMelee && (state_ == State::Approach || state_ == State::Idle)) {
						TransitionTo(State::Attack); // 崖で詰められない＋銃持ち → 撃つ体勢へ
					}
				}
			}
		}
	}

	// ================= ナビ: 穴・壁・場外・崖・低い隙間の先読み =================
	bool terrainBlocked = false;
	if (ctx.stage && desiredMoveX != 0.0f) {
		// 武器へ向かっているときは、少し無理めの穴でも跳ぶ（pickup の下には必ず床がある）。
		const bool fetching = (state_ == State::FetchWeapon);
		const float navJumpGap = fetching ? kAiMaxJumpGap * 1.4f : jumpGap;
		const AINav::MoveHazard hz = AINav::Probe(*ctx.stage, pin.selfPos, desiredMoveX,
			kFeetHalfY, kLookAhead, navJumpGap, kAiMaxJumpUp, kMaxSafeDrop);

		const bool recklessDive = reckless && p.targetBelow;
		// 武器のすぐ手前が穴なら、着地判定が渋くても跳ぶ。
		const bool fetchLeap = fetching && ctx.nearestPickup && hz.pitAhead && !hz.wallTall
			&& pickupDist < kAiMaxJumpGap * 1.8f;

		// 前方の穴に着地は無いが下段の床がある(dropAhead)場合、
		// 「下段のターゲット／武器へ向かっている」ときだけ歩いて飛び降りることを許す。
		const bool wantDescend =
			(state_ == State::FetchWeapon && ctx.nearestPickup
				&& ctx.nearestPickup->y < pin.selfPos.y - 0.5f)
			|| (p.targetBelow && (state_ == State::Approach || state_ == State::Attack));
		// 自滅を重ねたら、飛び降りでの追跡もやめる（穴系の判断を一律で慎重に）。
		const bool canDrop = hz.dropAhead && wantDescend && selfFallCount_ < 2;

		const bool hardStop = hz.edgeAhead
			|| (hz.pitAhead && !hz.jumpClears && !recklessDive && !canDrop && !fetchLeap)
			|| (hz.wallAhead && hz.wallTall); // 越えられない壁は押し込まない

		if (hz.crouchAhead) {
			out.crouchHeld = true;
		} else if (hardStop) {
			desiredMoveX = 0.0f;
			terrainBlocked = true;
		} else if (canDrop) {
			// desiredMoveX はそのまま。端まで歩いてそのまま落ちる（ジャンプしない）。
		} else if (hz.pitAhead && (hz.jumpClears || recklessDive || fetchLeap) && jumpCooldown_ <= 0.0f) {
			out.jumpTriggered = true;
			jumpCooldown_ = kJumpInterval;
		} else if (hz.wallAhead && !hz.wallTall && jumpCooldown_ <= 0.0f && !p.targetBelow) {
			out.jumpTriggered = true;
			jumpCooldown_ = kJumpInterval;
		}
	}

	terrainBlockedTimer_ = terrainBlocked ? (terrainBlockedTimer_ + dt) : 0.0f;

	// 足止めされたら棒立ちしない。ただし探索は 0.8 秒まで（それ以上は諦めて待つ＝オシレーション防止）。
	if (terrainBlocked && ctx.stage) {
		if (wantWeaponFetch && state_ != State::Retreat) {
			TransitionTo(State::FetchWeapon); // 崖越しの相手を諦めて武器を取りに行く
		} else if (p.hasLineOfSight && !pin.targetIsDead && state_ != State::Retreat
			&& state_ != State::FetchWeapon) {
			TransitionTo(State::Attack);
			reactionTimer_ = reactionDelay;
		} else if (!p.hasLineOfSight && terrainBlockedTimer_ < 0.8f) {
			const AINav::MoveHazard back = AINav::Probe(*ctx.stage, pin.selfPos, -towardX,
				kFeetHalfY, kLookAhead, kAiMaxJumpGap, kAiMaxJumpUp, kMaxSafeDrop);
			const bool backHardStop = back.edgeAhead || back.wallAhead
				|| (back.pitAhead && !back.jumpClears && !back.dropAhead);
			if (!backHardStop) {
				desiredMoveX = -towardX * 0.7f;
			}
		}
		// 0.8 秒探して打開できなければ desiredMoveX は 0 のまま待機する。
	}

	// 相手が上にいて近い → ジャンプで段差を登る。
	if (ctx.stage && p.targetAbove && p.horizontalGap < 2.5f && jumpCooldown_ <= 0.0f) {
		out.jumpTriggered = true;
		jumpCooldown_ = kJumpInterval;
	}

	// ================= 学習: 飛来弾の回避 =================
	// 1つの脅威につき1回だけ「避けられるか」を DodgeSkill で抽選する（序盤はほぼ避けない＝的）。
	if (ctx.incomingThreat && ctx.threatTtc > 0.0f && ctx.threatTtc < 0.45f) {
		if (!dodgeLatched_) {
			dodgeLatched_ = true;
			dodgeRoll_ = RandomGenerator::Instance().NextFloat01() < dodgeSkill;
		}
		if (dodgeRoll_) {
			const float threatDy = ctx.threatPos.y - pin.selfPos.y;
			if (threatDy > 0.4f) {
				out.crouchHeld = true;               // 高い弾 → しゃがむ
			} else if (jumpCooldown_ <= 0.0f) {
				out.jumpTriggered = true;            // 低い／水平の弾 → 跳ぶ
				jumpCooldown_ = kJumpInterval;
			}
			// 弾の進行方向と垂直に横ステップ（崖側でなければ）。
			const float stepDir = (ctx.threatVel.x >= 0.0f) ? 1.0f : -1.0f; // 弾が進む向きへ流す
			if (ctx.stage) {
				const AINav::MoveHazard h = AINav::Probe(*ctx.stage, pin.selfPos, stepDir,
					kFeetHalfY, kLookAhead, jumpGap, kAiMaxJumpUp, kMaxSafeDrop);
				if (!h.edgeAhead && !(h.pitAhead && !h.jumpClears && !h.dropAhead)) {
					desiredMoveX = stepDir;
				}
			}
		}
	} else {
		dodgeLatched_ = false;
	}

	// ================= 破綻潰し: 壁ドン検知 =================
	if (prevSelfValid_) {
		const float movedX = std::fabs(pin.selfPos.x - prevSelfX_);
		if (desiredMoveX != 0.0f && movedX < kStuckMoveEps
			&& !out.jumpTriggered && !terrainBlocked && !edgeHeld) {
			stuckTimer_ += dt;
		} else {
			stuckTimer_ = 0.0f;
		}
		if (stuckTimer_ > kStuckTime) {
			if (jumpCooldown_ <= 0.0f) {
				out.jumpTriggered = true;
				jumpCooldown_ = kJumpInterval;
			}
			reverseTimer_ = kStuckReverseTime;
			stuckTimer_ = 0.0f;
		}
	}
	if (reverseTimer_ > 0.0f) {
		reverseTimer_ -= dt;
		desiredMoveX = -towardX; // 一旦下がって別ルートを探す
	}
	prevSelfX_ = pin.selfPos.x;
	prevSelfValid_ = true;

	// ================= 移動方向のコミット（プルプル防止）=================
	// 一度動き出した向きは kMoveCommitTime のあいだ維持し、逆向きの要求は握りつぶす。
	// 地形で止められた／崖回避／壁ドン後退 のときは即座に従う。
	if (moveCommitTimer_ > 0.0f) {
		moveCommitTimer_ -= dt;
	}
	if (desiredMoveX != 0.0f && !terrainBlocked && !edgeHeld && reverseTimer_ <= 0.0f) {
		const float dir = (desiredMoveX > 0.0f) ? 1.0f : -1.0f;
		if (committedMoveDir_ != 0.0f && dir != committedMoveDir_ && moveCommitTimer_ > 0.0f) {
			desiredMoveX = committedMoveDir_ * std::fabs(desiredMoveX); // 逆向き要求を無視
		} else if (dir != committedMoveDir_) {
			committedMoveDir_ = dir;
			moveCommitTimer_ = kMoveCommitTime;
		}
	} else if (moveCommitTimer_ <= 0.0f) {
		committedMoveDir_ = 0.0f;
	}

	out.moveX = desiredMoveX;

	// ================= 攻撃入力 =================
	const float attackReach = wantMelee ? kMeleeRange * 1.2f : kRangedMax;
	const bool canAttackNow = (state_ == State::Attack) && !pin.targetIsDead
		&& (p.distance <= attackReach) && (p.hasLineOfSight || floorDrop);
	if (canAttackNow) {
		if (reactionTimer_ <= 0.0f) {
			out.attackHeld = true;
			if (attackRefireTimer_ <= 0.0f) {
				out.attackTriggered = true;
				attackRefireTimer_ = kAttackRefire;
			}
		}
	} else {
		reactionTimer_ = reactionDelay;
	}

	// ================= 学習: 撃ちながらの横移動（スペーシング） =================
	// 序盤(SpacingSkill≈0)は棒立ちで撃つ＝当てやすい的。上達すると左右に揺れて狙いにくくなる。
	if (canAttackNow && !wantMelee && spacingSkill > 0.05f && std::fabs(out.moveX) < 0.3f) {
		strafePhase_ += dt * (2.2f + 3.0f * spacingSkill);
		float strafe = std::sin(strafePhase_) * spacingSkill * 0.7f;
		const float sd = (strafe >= 0.0f) ? 1.0f : -1.0f;
		if (ctx.stage) {
			const AINav::MoveHazard h = AINav::Probe(*ctx.stage, pin.selfPos, sd,
				kFeetHalfY, kLookAhead, jumpGap, kAiMaxJumpUp, kMaxSafeDrop);
			if (h.edgeAhead || (h.pitAhead && !h.jumpClears && !h.dropAhead) || h.wallAhead) {
				strafe = 0.0f; // 崖・壁側へは揺れない
			}
		}
		out.moveX = strafe;
	} else {
		strafePhase_ = 0.0f;
	}

	// 武器投げ（成立フレームだけ）。投げると素手に戻り、次フレームから pickup を拾いに行く。
	if (wantThrowWeapon) {
		out.throwTriggered = true;
		throwCdTimer_ = kThrowVsCroucherCd;
	}

	// ================= 凍結対策 =================
	// 何も出力できない状態が続いたら（孤島に取り残された等）、強制的に行動する。
	const bool actedThisFrame = std::fabs(out.moveX) > 0.01f || out.attackHeld
		|| out.jumpTriggered || out.crouchHeld || out.throwTriggered;
	frozenTimer_ = actedThisFrame ? 0.0f : (frozenTimer_ + dt);
	if (frozenTimer_ > 1.0f && !pin.targetIsDead) {
		pickupBlacklistTimer_ = 0.0f; // 諦めていた pickup をやり直す
		bool handled = false;

		// 1) 武器があって射程・射線が通るなら撃つ（reactionTimer は無視＝もう十分待った）。
		if (!wantMelee && (p.hasLineOfSight || floorDrop) && p.distance <= kRangedMax) {
			out.attackHeld = true;
			if (attackRefireTimer_ <= 0.0f) {
				out.attackTriggered = true;
				attackRefireTimer_ = kAttackRefire;
			}
			handled = true;
		}

		// 2) 手ぶらなら武器へ強行（場外直行だけ回避、穴はジャンプ）。
		if (!handled && wantMelee && ctx.nearestPickup && selfFallCount_ < 2 && ctx.stage) {
			out.moveX = (ctx.nearestPickup->x >= pin.selfPos.x) ? 1.0f : -1.0f;
			const AINav::MoveHazard h = AINav::Probe(*ctx.stage, pin.selfPos, out.moveX,
				kFeetHalfY, kLookAhead, kAiMaxJumpGap * 1.5f, kAiMaxJumpUp, kMaxSafeDrop);
			if (h.edgeAhead) {
				out.moveX = 0.0f;
			} else {
				if (h.pitAhead && jumpCooldown_ <= 0.0f) {
					out.jumpTriggered = true;
					jumpCooldown_ = kJumpInterval;
				}
				handled = true;
			}
		}

		// 3) それでも打つ手なし → 相手に一番近い自足場の端まで詰めて待機する
		//    （撃てないなりに位置取りする。相手が射程に入れば 1) が発火して撃ち始める）。
		if (!handled && ctx.stage) {
			const float dir = towardX; // 相手の方向
			const AINav::MoveHazard h = AINav::Probe(*ctx.stage, pin.selfPos, dir,
				kFeetHalfY, 1.4f, 0.1f, kAiMaxJumpUp, kMaxSafeDrop);
			const bool atEdge = h.edgeAhead || h.wallAhead || (h.pitAhead && !h.dropAhead);
			out.moveX = atEdge ? 0.0f : dir * 0.7f; // 端でなければもう少し詰める、端なら止まって待つ
		}
	}

	dbg_.moveX = out.moveX;
	dbg_.edgeBias = edgeBias;
	dbg_.terrainBlocked = terrainBlocked;
	dbg_.wantFetch = wantWeaponFetch;
	dbg_.blacklisted = pickupBlacklisted;
	dbg_.pickupDist = ctx.nearestPickup ? pickupDist : -1.0f;
	dbg_.frozen = frozenTimer_;

	return out;
}
