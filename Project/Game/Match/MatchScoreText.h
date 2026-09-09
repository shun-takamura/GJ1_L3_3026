#pragma once

#include <string>
#include <string_view>

#include "TextRenderer.h"
#include "Vector2.h"
#include "Vector4.h"

/// <summary>
/// 「取ったラウンド」の画面表示(GameScene のラウンド決着バナー下 / ResultScene)で共有する
/// 配色と描画ヘルパ。
///
/// 数字がどちらのものか一目で分かるように、キャラクターモデルとまったく同じ色で塗る。
/// 色の定義元はここ 1 か所だけにして、GameScene::Initialize のモデル色もこれを使う。
/// </summary>
namespace MatchScoreText {

	// キャラクターモデルの色(GameScene::Initialize の SetupAnimatedModel へ渡すものと同じ)。
	inline constexpr Vector4 kPlayerColor{ 0.28f, 0.55f, 1.0f, 1.0f };  // プレイヤー = 青
	inline constexpr Vector4 kEnemyColor{ 1.0f, 0.32f, 0.28f, 1.0f };  // 敵 = 赤

	// 左右の間に挟む区切り(白のまま)。
	inline constexpr const char* kSeparator = " - ";

	/// <summary>
	/// left(プレイヤー色) + " - "(白) + right(敵色) を、centerX を中心に横並びで描く。
	/// y は文字列の上端ピクセル座標。TextRenderer::Flush は呼び出し側の責務。
	/// </summary>
	inline void DrawCenteredPair(TextRenderer* tr, std::string_view left, std::string_view right,
		float centerX, float y, float scale) {
		if (!tr) {
			return;
		}

		const float lw = tr->MeasureWidth(left, scale);
		const float sw = tr->MeasureWidth(kSeparator, scale);
		const float rw = tr->MeasureWidth(right, scale);

		float x = centerX - (lw + sw + rw) * 0.5f;
		tr->DrawText(left, { x, y }, scale, kPlayerColor);
		x += lw;
		tr->DrawText(kSeparator, { x, y }, scale);
		x += sw;
		tr->DrawText(right, { x, y }, scale, kEnemyColor);
	}

	/// <summary>
	/// 得点そのもの("3" と "2")を DrawCenteredPair で描く。
	/// </summary>
	inline void DrawCenteredScore(TextRenderer* tr, int playerPoints, int enemyPoints,
		float centerX, float y, float scale) {
		DrawCenteredPair(tr, std::to_string(playerPoints), std::to_string(enemyPoints),
			centerX, y, scale);
	}

} // namespace MatchScoreText
