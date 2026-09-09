#include "Tutorial/TutorialEnemyBrain.h"

#include <cmath>

#include "AI/AINavigation.h"
#include "Character/Character.h"
#include "Common/IStageQuery.h"

void TutorialEnemyBrain::Reset() {
	jumpCooldown_ = 0.0f;
	moveCommitTimer_ = 0.0f;
	committedDir_ = 0.0f;
	holdingDistance_ = false;
}

CharacterInput TutorialEnemyBrain::Think(const Character& self, const Character& target,
	const IStageQuery* stage, float dt) {
	CharacterInput out;
	// この AI は絶対に攻撃しない（チュートリアル中はプレイヤーを一方的に殴らせる）。
	out.attackTriggered = false;
	out.attackHeld = false;
	out.throwTriggered = false;

	if (jumpCooldown_ > 0.0f) jumpCooldown_ -= dt;
	if (moveCommitTimer_ > 0.0f) moveCommitTimer_ -= dt;

	const Vector3 selfPos = self.GetPosition();
	const Vector3 targetPos = target.GetPosition();

	// 常に相手の方を向く（見た目の向き・アニメの都合。撃たないので照準精度は不要）。
	const float toTargetX = targetPos.x - selfPos.x;
	const float toTargetY = targetPos.y - selfPos.y;
	out.aimDirX = (std::fabs(toTargetX) < 0.001f && std::fabs(toTargetY) < 0.001f) ? 1.0f : toTargetX;
	out.aimDirY = toTargetY;

	if (!stage || target.IsDead() || self.IsDead()) {
		return out; // 相手が居ない/死んでいるなら、その場で立っているだけ
	}

	//====================
	// 進みたい向きを決める。近づきすぎたら足を止め、離れたらまた寄る
	// （kKeepDistance / kResumeDistance のヒステリシスで、境界でのプルプルを防ぐ）。
	//====================
	const float distX = std::fabs(toTargetX);
	if (holdingDistance_) {
		if (distX > kResumeDistance) holdingDistance_ = false;
	} else {
		if (distX < kKeepDistance) holdingDistance_ = true;
	}

	float desiredDir = 0.0f;
	if (!holdingDistance_) {
		desiredDir = (toTargetX >= 0.0f) ? 1.0f : -1.0f;
		// 動き出した向きは少しの間キープする（相手が真横で行ったり来たりしても揺れない）。
		if (moveCommitTimer_ > 0.0f && committedDir_ != 0.0f) {
			desiredDir = committedDir_;
		}
	}

	//====================
	// 危険地形の先読み。穴・場外・トゲには自分から踏み込まない。
	//====================
	auto probe = [&](float dir) {
		return AINav::Probe(*stage, selfPos, dir, kFeetHalfY, kLookAhead,
			kMaxJumpGap, kMaxJumpUp, kMaxSafeDrop);
	};
	// 「その方向へ 1 歩も踏み出してはいけない」条件。
	// dropAhead(kMaxSafeDrop 以内に着地できる床があると先読みで分かっている崖)は穴扱いしない
	// ── 着地が保証されているので自滅にはならず、これを塞ぐと高台のスポーンから降りられなくなる。
	auto hardStop = [](const AINav::MoveHazard& h) {
		return h.edgeAhead
			|| (h.spikeAhead && !h.jumpClears)
			|| (h.pitAhead && !h.jumpClears && !h.dropAhead);
	};

	if (desiredDir != 0.0f) {
		AINav::MoveHazard hz = probe(desiredDir);
		if (hardStop(hz)) {
			// 相手の方へは行けない。逆方向が安全ならそちらへ下がって「動いている」状態を保つ。
			const float away = -desiredDir;
			const AINav::MoveHazard back = probe(away);
			if (!hardStop(back)) {
				desiredDir = away;
				hz = back;
			} else {
				desiredDir = 0.0f; // どちらも危険。その場で待つ
			}
		}

		if (desiredDir != 0.0f) {
			out.moveX = desiredDir;

			// 跳び越せる穴／越えられる段差ならジャンプする。
			const bool wantJump = (hz.pitAhead && hz.jumpClears)
				|| (hz.spikeAhead && hz.jumpClears)
				|| (hz.wallAhead && !hz.wallTall && !hz.crouchAhead);
			if (wantJump && self.IsGrounded() && jumpCooldown_ <= 0.0f) {
				out.jumpTriggered = true;
				jumpCooldown_ = kJumpInterval;
			}
			// 頭上だけ塞がった隙間はしゃがんで通る。
			if (hz.crouchAhead) {
				out.crouchHeld = true;
			}

			if (committedDir_ != desiredDir) {
				committedDir_ = desiredDir;
				moveCommitTimer_ = kMoveCommitTime;
			}
		}
	}

	//====================
	// ベルトコンベア対策。足元のベルトが危険な方へ流しているなら逆走する
	// （移動の最終決定なので、上で決めた moveX を上書きする）。
	//====================
	if (self.IsGrounded()) {
		const int beltDir = stage->BeltDirUnderAabb(self.GetColliderCenter(),
			self.GetColliderHalfExtent(), 0.2f);
		if (beltDir != 0) {
			const float beltF = static_cast<float>(beltDir);
			if (hardStop(probe(beltF))) {
				// 流された先が穴／トゲ／場外。逆向きが安全なら逆走して耐える。
				const float against = -beltF;
				if (!hardStop(probe(against))) {
					out.moveX = against;
					out.crouchHeld = false; // しゃがむと歩く速度が落ちて流され切ってしまう
				}
			}
		}
	}

	return out;
}
