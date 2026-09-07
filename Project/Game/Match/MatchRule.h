#pragma once

/// <summary>
/// 1セットぶんの得点(プレイヤー/敵)と、10ポイント先取での勝敗判定だけを持つ小さいクラス。
///
/// 場外/HP0の判定そのものや、判定後のキャラ・ステージのリセットは GameScene の責務のまま。
/// このクラスは「今何点か」「どちらが勝ったか」だけを覚えている。
/// </summary>
class MatchRule {
public:
	enum class Winner {
		None,   // 未決着(セット継続中)
		Player,
		Enemy,
	};

	// 先取ポイント数。企画書の「10ポイント先取制」に対応
	// (PlayerModel::kMaxTier と同じ 10 を使っている)。
	static constexpr int kPointsToWin = 10;

	/// <summary>両者の得点を0に戻し、勝者もクリアする。新しいセットの開始時に呼ぶ。</summary>
	void Reset();

	/// <summary>
	/// side に1点加算する。加算後に kPointsToWin へ達していれば winner をセットして true を返す
	/// (=このセットが決着した)。まだ決着していなければ false。
	/// </summary>
	bool AddPoint(Winner side);

	int GetPlayerPoints() const { return playerPoints_; }
	int GetEnemyPoints() const { return enemyPoints_; }
	Winner GetWinner() const { return winner_; }

private:
	int playerPoints_ = 0;
	int enemyPoints_ = 0;
	Winner winner_ = Winner::None;
};
