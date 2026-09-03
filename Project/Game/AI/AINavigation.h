#pragma once

#include "Vector3.h"

class IStageQuery;

/// <summary>
/// カメラ固定の横視点なので、経路探索は「今の進行方向の1〜2マス先に
/// 穴／壁／場外が無いか」を見るだけで足りる。その先読み結果。
/// </summary>
namespace AINav {

	struct MoveHazard {
		bool pitAhead = false;   // 進行方向の足元に床が無い（落ちる）
		bool wallAhead = false;  // 進行方向の目の前が地形（詰まる）
		bool edgeAhead = false;  // これ以上進むとステージ範囲外（場外）
	};

	/// <summary>
	/// pos から dirX 方向（符号のみ使う）へ lookAhead だけ先を調べる。
	/// stage には IStageQuery の OverlapsSolid / IsPointInsideBounds しか使わない
	/// （A はセルの中身を直接見ない、という取り決めに合わせている）。
	/// </summary>
	/// <param name="feetHalfY">キャラ半身の高さ（足元〜中心の距離）。</param>
	MoveHazard Probe(const IStageQuery& stage, const Vector3& pos, float dirX,
		float feetHalfY, float lookAhead);

}
