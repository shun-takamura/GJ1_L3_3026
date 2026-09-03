#include "AI/AINavigation.h"

#include "Common/IStageQuery.h"

namespace AINav {

	MoveHazard Probe(const IStageQuery& stage, const Vector3& pos, float dirX,
		float feetHalfY, float lookAhead) {
		MoveHazard hz;

		const float sign = (dirX >= 0.0f) ? 1.0f : -1.0f;
		const Vector3 ahead{ pos.x + sign * lookAhead, pos.y, pos.z };

		// 場外: これ以上その方向へ進むとグリッド外／落下ラインを割る。
		hz.edgeAhead = !stage.IsPointInsideBounds(ahead);

		// 壁: 進行方向の胴体位置に地形が重なっている。
		const Vector3 bodyHalf{ 0.1f, feetHalfY * 0.8f, 0.1f };
		hz.wallAhead = stage.OverlapsSolid(ahead, bodyHalf);

		// 穴: 進行方向の足元より下、約1.5マスぶんを探って床が無ければ穴。
		//     壁があるなら「穴」ではなく「登る対象」なので pit にはしない。
		if (!hz.wallAhead) {
			const Vector3 belowCenter{ ahead.x, ahead.y - feetHalfY - 0.9f, ahead.z };
			const Vector3 belowHalf{ 0.3f, 0.9f, 0.3f };
			hz.pitAhead = !stage.OverlapsSolid(belowCenter, belowHalf);
		}

		return hz;
	}

}
