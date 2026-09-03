#include "AI/AINavigation.h"

#include "Common/IStageQuery.h"

namespace AINav {

	namespace {
		// x の位置で、足元 feetY から down だけ下までの間に地形があるか。
		bool GroundWithin(const IStageQuery& s, float x, float feetY, float z, float down) {
			const Vector3 c{ x, feetY - down * 0.5f, z };
			const Vector3 h{ 0.3f, down * 0.5f, 0.3f };
			return s.OverlapsSolid(c, h);
		}

		// x の位置、指定の縦帯 (centerY ± halfY) に地形が食い込んでいるか。
		bool SolidBand(const IStageQuery& s, float x, float centerY, float z, float halfY) {
			return s.OverlapsSolid({ x, centerY, z }, { 0.12f, halfY, 0.12f });
		}
	}

	MoveHazard Probe(const IStageQuery& stage, const Vector3& pos, float dirX,
		float feetHalfY, float lookAhead, float maxJumpGap, float maxJumpUp, float maxSafeDrop) {
		MoveHazard hz;

		const float sign = (dirX >= 0.0f) ? 1.0f : -1.0f;
		const float feetY = pos.y - feetHalfY;
		const float x0 = pos.x + sign * lookAhead;

		// 場外: その方向はグリッド外／落下ラインの外。
		if (!stage.IsPointInsideBounds({ x0, pos.y, pos.z })) {
			hz.edgeAhead = true;
			return hz;
		}

		// 胴体を「脚(feetY〜中心)」と「頭(中心〜頭頂)」に分けて壁を見る。
		//   脚が塞がっている            → しゃがんでも無理な本物の壁
		//   頭だけ塞がっている＋床がある → しゃがみ歩きで通れる隙間
		const float bandHalf = feetHalfY * 0.5f;
		const bool legBlocked = SolidBand(stage, x0, pos.y - bandHalf, pos.z, bandHalf);
		const bool headBlocked = SolidBand(stage, x0, pos.y + bandHalf, pos.z, bandHalf);
		const bool groundAhead = GroundWithin(stage, x0, feetY, pos.z, 1.0f);

		if (legBlocked) {
			hz.wallAhead = true;
			// 壁の上端が「頭 + maxJumpUp」より高ければ、ジャンプしても越えられない。
			hz.wallTall = SolidBand(stage, x0, pos.y + feetHalfY + maxJumpUp, pos.z, feetHalfY * 0.5f);
			return hz;
		}
		if (headBlocked) {
			if (groundAhead) {
				hz.crouchAhead = true; // 頭上の隙間をくぐる
			} else {
				hz.wallAhead = true;   // 頭は塞がり足元は穴 = 進めない
				hz.wallTall = true;    // 頭上が塞がっている以上ジャンプは無意味
			}
			return hz;
		}
		if (groundAhead) {
			return hz; // まっすぐ歩いて安全
		}

		// ここから穴。maxJumpGap ぶん先まで着地できる床を探す。
		hz.pitAhead = true;
		const float step = 0.5f;
		for (float d = lookAhead + step; d <= lookAhead + maxJumpGap + 0.01f; d += step) {
			const float x = pos.x + sign * d;
			if (!stage.IsPointInsideBounds({ x, pos.y, pos.z })) {
				break; // 穴の先が場外 = 飛んでも落ちる
			}
			const bool landing = GroundWithin(stage, x, feetY + maxJumpUp, pos.z, maxJumpUp + 2.5f);
			const bool blocked = SolidBand(stage, x, pos.y + 0.3f, pos.z, feetHalfY * 0.6f);
			if (landing && !blocked) {
				hz.jumpClears = true;
				break;
			}
		}

		// 前方に着地は無いが、真下〜maxSafeDrop に床があれば「歩いて飛び降りれば着地できる」。
		if (!hz.jumpClears && GroundWithin(stage, x0, feetY, pos.z, maxSafeDrop)) {
			hz.dropAhead = true;
		}
		return hz;
	}

}
