#include "Match/MatchRule.h"

void MatchRule::Reset() {
	playerPoints_ = 0;
	enemyPoints_ = 0;
	winner_ = Winner::None;
}

bool MatchRule::AddPoint(Winner side) {
	if (side == Winner::Player) {
		playerPoints_ += 1;
	} else if (side == Winner::Enemy) {
		enemyPoints_ += 1;
	}

	if (playerPoints_ >= kPointsToWin) {
		winner_ = Winner::Player;
	} else if (enemyPoints_ >= kPointsToWin) {
		winner_ = Winner::Enemy;
	}
	return winner_ != Winner::None;
}
