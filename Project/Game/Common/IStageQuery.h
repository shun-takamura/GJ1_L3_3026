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

	/// <summary>
	/// 線分 a→b の途中に solid セル（壊れない床・壊れる床）が有るか。
	/// AI の射線チェック（相手が地形に隠れて撃てないかの判定）に使う。
	/// ※ IStageQuery 正式版の凍結対象に含める（B の LOS 判定でも必要になる想定）。
	/// </summary>
	virtual bool SegmentHitsSolid(const Vector3& a, const Vector3& b) const = 0;

	/// <summary>
	/// pos の足元の足場が「壊れる床」で、かつそのすぐ下に落下を受け止める床が
	/// 無いか（＝そこを撃てば下へ落とせる危うい足場）。
	/// AI が「プレイヤーの立つ足場を崩す」判断に使う。
	/// ※ IStageQuery 正式版の凍結対象に含める（B の危険床表示等でも使える想定）。
	/// </summary>
	virtual bool IsPrecariousBreakableFloor(const Vector3& pos) const = 0;
};
