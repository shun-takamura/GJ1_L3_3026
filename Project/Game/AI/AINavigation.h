#pragma once

#include "Vector3.h"

class IStageQuery;

/// <summary>
/// カメラ固定の横視点なので、経路判断は「今の進行方向の先に穴／壁／崖／場外が
/// 無いか、その穴は飛び越せるか」を見るだけで足りる。その先読み結果。
/// </summary>
namespace AINav {

	struct MoveHazard {
		bool pitAhead = false;    // 進行方向の足元に床が無い
		bool wallAhead = false;   // 進行方向の目の前が地形（詰まる。しゃがんでも無理）
		bool edgeAhead = false;   // これ以上進むとステージ範囲外
		bool jumpClears = false;  // pitAhead のとき、ジャンプすれば向こうに着地できる
		bool crouchAhead = false; // 頭上だけ塞がった隙間。しゃがみ歩きで通れる
		bool dropAhead = false;   // 前方は穴だが、maxSafeDrop 以内に下段の床がある（歩いて飛び降りれば着地）
		bool wallTall = false;    // wallAhead のとき、上端が maxJumpUp より高い＝ジャンプでは越えられない
	};

	/// <summary>
	/// pos から dirX 方向（符号のみ使う）へ先読みする。
	/// stage には IStageQuery の OverlapsSolid / IsPointInsideBounds しか使わない。
	///
	/// hardStop 条件（呼び出し側で判定）:
	///   edgeAhead || (pitAhead && !jumpClears)
	///   → この場合はその方向へ 1 歩も踏み出さない・ジャンプもしない。
	/// </summary>
	/// <param name="feetHalfY">キャラ半身の高さ（足元〜中心の距離）。</param>
	/// <param name="lookAhead">胴体1つぶん前を見る距離。</param>
	/// <param name="maxJumpGap">横に飛び越せる穴の最大幅。</param>
	/// <param name="maxJumpUp">飛び乗れる段差の最大高。</param>
	/// <param name="maxSafeDrop">飛び降りても着地できるとみなす最大落差。</param>
	MoveHazard Probe(const IStageQuery& stage, const Vector3& pos, float dirX,
		float feetHalfY, float lookAhead, float maxJumpGap, float maxJumpUp, float maxSafeDrop);

}
