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

		// トゲ（32 等の即死ギミック）は solid ではないので下の床／壁判定には現れない。
		// 1 歩先の「足元少し下 〜 頭頂」の縦帯にトゲが掛かっているかを別に見る。
		{
			const float loY = feetY - 0.4f;
			const float hiY = pos.y + feetHalfY;
			hz.spikeAhead = stage.OverlapsSpike(
				{ x0, (loY + hiY) * 0.5f, pos.z },
				{ 0.24f, (hiY - loY) * 0.5f, 0.24f });
		}

		// 胴体を「脚(feetY〜中心)」と「頭(中心〜頭頂)」に分けて壁を見る。
		//   脚が塞がっている            → しゃがんでも無理な本物の壁
		//   頭だけ塞がっている＋床がある → しゃがみ歩きで通れる隙間
		const float bandHalf = feetHalfY * 0.5f;
		const bool legBlocked = SolidBand(stage, x0, pos.y - bandHalf, pos.z, bandHalf);
		const bool headBlocked = SolidBand(stage, x0, pos.y + bandHalf, pos.z, bandHalf);
		const bool groundAhead = GroundWithin(stage, x0, feetY, pos.z, 1.0f);

		// 前方の壁セルが「壊れる床」か（脚・頭どちらの高さで塞がれていても拾う）。
		auto wallIsBreakable = [&]() {
			return stage.IsBreakableAt({ x0, pos.y - bandHalf, pos.z })
				|| stage.IsBreakableAt({ x0, pos.y + bandHalf, pos.z });
		};

		if (legBlocked) {
			hz.wallAhead = true;
			// ジャンプで上に立てるのは「足元 + maxJumpUp」まで。そこより上へ壁が続いていれば
			// 跳んでも乗れない＝越えられない。
			//   band = [feetY + maxJumpUp, feetY + maxJumpUp + feetHalfY]
			// （旧実装は pos.y + feetHalfY + maxJumpUp を見ており、2 セル以上の壁でも
			//   wallTall=false になって永久ジャンプしていた）。
			hz.wallTall = SolidBand(stage, x0,
				feetY + maxJumpUp + feetHalfY * 0.5f, pos.z, feetHalfY * 0.5f);
			hz.breakableAhead = wallIsBreakable();
			return hz;
		}
		if (headBlocked) {
			if (groundAhead) {
				hz.crouchAhead = true; // 頭上の隙間をくぐる
			} else {
				hz.wallAhead = true;   // 頭は塞がり足元は穴 = 進めない
				hz.wallTall = true;    // 頭上が塞がっている以上ジャンプは無意味
				hz.breakableAhead = wallIsBreakable();
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
			// 着地セルにトゲがあるならそこは着地点として認めない（跳んだ先で即死しない）。
			const bool landingSpiked = stage.OverlapsSpike(
				{ x, feetY, pos.z }, { 0.3f, feetHalfY + maxJumpUp, 0.3f });
			if (landing && !blocked && !landingSpiked) {
				hz.jumpClears = true;
				break;
			}
		}

		// 前方に着地は無いが、真下〜maxSafeDrop に床があれば「歩いて飛び降りれば着地できる」。
		// ただし飛び降り先にトゲがあるなら降りない。
		if (!hz.jumpClears && GroundWithin(stage, x0, feetY, pos.z, maxSafeDrop)
			&& !stage.OverlapsSpike({ x0, feetY - maxSafeDrop * 0.5f, pos.z },
				{ 0.24f, maxSafeDrop * 0.5f, 0.24f })) {
			hz.dropAhead = true;
		}
		return hz;
	}

}
