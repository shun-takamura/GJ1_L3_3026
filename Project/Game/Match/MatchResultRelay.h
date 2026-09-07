#pragma once

#include "Match/MatchRule.h"

/// <summary>
/// GameScene で決着したセットの勝者を、次に生成される ResultScene へ橋渡しするだけの relay。
///
/// SceneFactory::CreateScene はシーン名しか受け取らず、シーン間でデータを渡す仕組みが
/// 今のところ無いため、GameScene::s_activeForDebug_ と同じ「static で受け渡す」やり方を踏襲している。
/// </summary>
namespace MatchResultRelay {

	/// <summary>GameScene が Result シーンへ遷移する直前に、決着時点の MatchRule の中身をまとめて渡す。</summary>
	void SetResult(MatchRule::Winner winner, int playerPoints, int enemyPoints);

	/// <summary>以下3つは ResultScene::Initialize から呼ぶ。</summary>
	MatchRule::Winner GetWinner();
	int GetPlayerPoints();
	int GetEnemyPoints();

} // namespace MatchResultRelay
