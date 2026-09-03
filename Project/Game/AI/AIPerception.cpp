#include "AI/AIPerception.h"

#include <cmath>

#include "Common/IStageQuery.h"

PerceptionResult AIPerception::Sense(const PerceptionInput& in) const {
	PerceptionResult r;

	const float dx = in.targetPos.x - in.selfPos.x;
	const float dy = in.targetPos.y - in.selfPos.y;
	const float dist = std::sqrt(dx * dx + dy * dy);

	r.distance = dist;
	r.horizontalGap = std::fabs(dx);
	if (dist > 1e-4f) {
		r.dirToTargetX = dx / dist;
		r.dirToTargetY = dy / dist;
	} else {
		r.dirToTargetX = 1.0f;
		r.dirToTargetY = 0.0f;
	}

	r.targetAbove = (dy > kVerticalStep);
	r.targetBelow = (-dy > kVerticalStep);

	if (in.selfMaxHp > 0.0f) {
		r.selfLowHp = (in.selfHp <= in.selfMaxHp * kLowHpRatio);
	}

	// 射線チェック: 足元から kEyeHeight だけ上げた点どうしを結んで、
	// 途中に地形が無ければ「撃てる」。相手が死んでいるなら撃つ必要はない。
	if (!in.targetIsDead && in.stage) {
		const Vector3 eyeA{ in.selfPos.x, in.selfPos.y + kEyeHeight, in.selfPos.z };
		const Vector3 eyeB{ in.targetPos.x, in.targetPos.y + kEyeHeight, in.targetPos.z };
		r.hasLineOfSight = !in.stage->SegmentHitsSolid(eyeA, eyeB);
	} else if (!in.targetIsDead && !in.stage) {
		r.hasLineOfSight = true; // ステージ未設定（単体テスト等）は遮蔽なし扱い
	}

	return r;
}
