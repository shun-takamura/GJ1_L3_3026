#pragma once

#include "Common/CharacterInput.h"

class Character;
class IStageQuery;

/// <summary>
/// チュートリアル専用の敵 AI。
///
/// EnemyBrain（本編の敵）とは別物で、意図的に「動くだけの的」に振ってある:
///   - 攻撃・武器投げ・武器拾いは一切出力しない（attackTriggered/throwTriggered は常に false）。
///   - プレイヤーの方をゆるく追うが、一定距離まで近づいたら止まる（押し込んで事故らせない）。
///   - 穴・場外・トゲには自分から踏み込まない（AINav::Probe の先読みで hardStop）。
///     跳び越せる穴・越えられる段差だけジャンプし、頭上が塞がった隙間はしゃがんで通る。
///   - ベルトコンベアで危険な方へ流されていたら逆走する。
///
/// つまり「プレイヤーが説明を読みながら安全に殴れて、かつ棒立ちではない相手」を作るのが目的。
/// 出力は EnemyBrain と同じく CharacterInput 1つだけなので、Character 側は本編と全く同じ経路で動く。
/// </summary>
class TutorialEnemyBrain {
public:
	/// <summary>内部の移動コミット・クールダウンを初期化する（開始時・位置リセット時に呼ぶ）。</summary>
	void Reset();

	/// <summary>このフレームの行動を返す。stage が null なら「その場で相手を向くだけ」。</summary>
	CharacterInput Think(const Character& self, const Character& target,
		const IStageQuery* stage, float dt);

private:
	// ---- ナビの先読み寸法（EnemyBrain の同名定数と揃えてある。Character の
	//      ジャンプ初速・重力に対して確実に届く控えめな値）----
	static constexpr float kFeetHalfY = 0.9f;
	static constexpr float kLookAhead = 1.2f;
	static constexpr float kMaxJumpGap = 2.0f;  // 跳び越せる穴の最大幅（安全側に狭く取る）
	static constexpr float kMaxJumpUp = 1.8f;   // 飛び乗れる段差の最大高
	// 「この落差以内に着地できる床がある」ことが先読みで確定した崖からだけ飛び降りる。
	// 0 にすると高台のスポーンから一切降りられず、敵が足場に取り残されて棒立ちになる。
	static constexpr float kMaxSafeDrop = 4.0f;

	// ---- 間合い ----
	static constexpr float kKeepDistance = 1.4f; // これより近づいたら足を止める
	static constexpr float kResumeDistance = 2.2f; // 一度止まったらここまで離れるまで動かない

	// ---- タイミング ----
	static constexpr float kJumpInterval = 0.6f;   // 連続ジャンプの間隔
	static constexpr float kMoveCommitTime = 0.25f; // 動き出した向きを最低この秒数キープ（プルプル防止）

	float jumpCooldown_ = 0.0f;
	float moveCommitTimer_ = 0.0f;
	float committedDir_ = 0.0f;
	bool holdingDistance_ = false; // 相手に寄り切って足を止めている状態か
};
