#include "Match/MatchResultRelay.h"

namespace {
	MatchRule::Winner g_lastWinner = MatchRule::Winner::None;
	int g_lastPlayerPoints = 0;
	int g_lastEnemyPoints = 0;
}

namespace MatchResultRelay {

	void SetResult(MatchRule::Winner winner, int playerPoints, int enemyPoints) {
		g_lastWinner = winner;
		g_lastPlayerPoints = playerPoints;
		g_lastEnemyPoints = enemyPoints;
	}

	MatchRule::Winner GetWinner() {
		return g_lastWinner;
	}

	int GetPlayerPoints() {
		return g_lastPlayerPoints;
	}

	int GetEnemyPoints() {
		return g_lastEnemyPoints;
	}

} // namespace MatchResultRelay
