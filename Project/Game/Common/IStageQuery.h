#pragma once

#include "Vector3.h"

/// <summary>
/// ステージ形状への問い合わせインターフェース（今日の最小版）。
///
/// Character はこれ越しにだけ地形へ触る。CSV かどうか・床に穴があるか・
/// アリーナが四角いかどうかは一切知らない（Character.h の設計コメントと対）。
///
/// ※ これは「csv サンプルを触れるようにする」ための最小版。
///   タスクリスト末尾の正式な IStageQuery（セルグリッド版 + QueryGimmick）へ
///   Day1 で差し替え、B と凍結し直す。
/// </summary>
struct StageMoveResult {
	Vector3 position{};        // 解決後の AABB 中心座標
	bool grounded = false;     // このフレーム、下方向で地形に接触した（＝接地）
	bool hitCeiling = false;   // 上方向で地形に接触した
	bool hitWall = false;      // 左右で地形に接触した
};

class IStageQuery {
public:
	virtual ~IStageQuery() = default;

	/// <summary>1 セルのワールドサイズ。</summary>
	virtual float GetCellSize() const = 0;

	/// <summary>p がステージの矩形範囲内か（範囲外＝場外）。</summary>
	virtual bool IsPointInsideBounds(const Vector3& p) const = 0;

	/// <summary>
	/// 中心 center・半サイズ half の AABB が solid セルと重なっているか。
	/// しゃがみ解除時に「立ち上がる空間があるか」を調べるのに使う。
	/// </summary>
	virtual bool OverlapsSolid(const Vector3& center, const Vector3& half) const = 0;

	/// <summary>
	/// 半サイズ half の AABB を中心 from から to へ動かし、solid セルとの
	/// めり込みを解消した結果を返す。X 軸 → Y 軸の順に分離して解く
	/// （同時に解こうとすると壁に張り付くため）。
	/// </summary>
	virtual StageMoveResult MoveAabb(const Vector3& from, const Vector3& to,
		const Vector3& half) const = 0;
};
