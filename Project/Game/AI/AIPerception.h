#pragma once

#include "Vector3.h"

class IStageQuery;

/// <summary>索敵の入力。EnemyBrain が毎フレーム埋めて Sense() に渡す。</summary>
struct PerceptionInput {
	Vector3 selfPos{};
	Vector3 targetPos{};
	float selfHp = 100.0f;
	float selfMaxHp = 100.0f;
	bool targetIsDead = false;
	const IStageQuery* stage = nullptr;
};

/// <summary>
/// 索敵結果。EnemyBrain はこの構造体だけを見て状態遷移・照準を決める
/// （生の座標計算・レイマーチをブレインに持ち込まないための分離）。
/// </summary>
struct PerceptionResult {
	float distance = 0.0f;        // 相手までの距離（XY 平面）
	float horizontalGap = 0.0f;   // 相手までの X 距離の絶対値
	float dirToTargetX = 1.0f;    // 相手方向の正規化ベクトル（横視点なので実質 ±1 寄り）
	float dirToTargetY = 0.0f;
	bool hasLineOfSight = false;  // 自分と相手の間に地形が無い（撃てる）
	bool targetAbove = false;     // 相手が自分より段差ぶん上にいる
	bool targetBelow = false;     // 相手が自分より段差ぶん下にいる
	bool selfLowHp = false;       // 自分の HP が撤退を考える水準を割った
};

/// <summary>
/// 索敵・射線チェックだけを担当する。状態は持たない（毎フレーム使い捨て）。
/// </summary>
class AIPerception {
public:
	PerceptionResult Sense(const PerceptionInput& in) const;

	// EnemyBrain 側でも参照したいしきい値。
	static constexpr float kLowHpRatio = 0.22f;  // maxHP のこの割合を割ったら selfLowHp（低め＝粘る）
	static constexpr float kVerticalStep = 1.2f; // これ以上の高低差で targetAbove / targetBelow
	static constexpr float kEyeHeight = 0.5f;    // 射線チェックで足元から少し上げる高さ
};
