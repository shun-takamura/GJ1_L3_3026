#pragma once

class Character;

/// <summary>
/// プレイヤーの行動を試合を通して観測し、AI がポイントを取られるたびに
/// 「対抗の強さ」を1段ずつ上げていく。本物の機械学習ではなく、
/// 統計 + 難易度ティアによる「学習しているように見える」実装（計画コメント参照）。
///
/// 【Day4（今）】ティアに応じた反応速度・エイム精度のスカラーだけを EnemyBrain に渡す。
/// 　　　　　　　観測カウンタは枠だけ用意し、埋められるものだけ埋める。
/// 【Day5】下の観測結果を使って EnemyBrain に個別の対抗行動を生やす:
///   - CrouchRatio が高い       → しゃがみ回避を読んで近接／投擲／山なり弾に切替
///   - LikesCampingBreakable    → プレイヤーの立つ壊れ床を優先的に撃って落とす
///   - PreferredRange           → その間合いを外す（近接特化なら離れてカイト 等）
///   - 移動予測（発砲後の移動癖）→ 置き撃ち
///
/// ステージを跨いで生き残る必要がある（得点ごとに GameScene を作り直す設計なら
/// この寿命管理は B の match/Rule コントローラ側へ移す。前回の相談事項）。
/// </summary>
class PlayerModel {
public:
	void Reset();

	/// <summary>毎フレーム、プレイヤーの状態を観測して統計へ積む。</summary>
	void Observe(const Character& player, const Character& enemy, float dt);

	/// <summary>enemy がポイントを取られた瞬間に呼ぶ。難易度ティアを1つ上げる。</summary>
	void OnPointConceded();

	//==============================
	// EnemyBrain が読む調整値（Day4 で有効）
	//==============================
	int   Tier() const { return tier_; }
	float ReactionDelay() const;  // 秒。ティアが上がるほど短い（＝速く反応する）
	float AimErrorRad() const;    // ラジアン。ティアが上がるほど小さい（＝正確になる）

	//==============================
	// Day5 の対抗行動が読む観測結果（今は近似値 or 未計測。TODO を残す）
	//==============================
	float PreferredRange() const;                 // プレイヤーが保ちがちな間合い（計測済み）
	float CrouchRatio() const;                    // TODO(Day5): Character のしゃがみ参照 or GameScene からのイベントが必要
	float JumpsPerSecond() const;                 // TODO(Day5): 接地状態の参照が必要
	bool  LikesCampingBreakable() const { return campingBreakableFloor_; } // TODO(Day5)

private:
	static constexpr int   kMaxTier = 10; // 10 ポイント先取なので最大 10 段
	static constexpr float kReactionAtTier0 = 0.40f;
	static constexpr float kReactionAtMaxTier = 0.10f;
	static constexpr float kAimErrAtTier0 = 0.20f;
	static constexpr float kAimErrAtMaxTier = 0.02f;

	int tier_ = 0;

	// ---- 観測の生カウンタ ----
	float observedTime_ = 0.0f;
	float rangeSum_ = 0.0f;
	int   rangeSamples_ = 0;
	bool  campingBreakableFloor_ = false; // TODO(Day5)
};
