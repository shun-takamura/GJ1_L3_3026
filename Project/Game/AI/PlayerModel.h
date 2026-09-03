#pragma once

class Character;
class IStageQuery;

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
	void Observe(const Character& player, const Character& enemy,
		const IStageQuery* stage, float dt);

	/// <summary>enemy がポイントを取られた瞬間に呼ぶ。難易度ティアを1つ上げる。</summary>
	void OnPointConceded();

	//==============================
	// EnemyBrain が読む調整値（Day4 で有効）
	//==============================
	int   Tier() const { return tier_; }
	float ReactionDelay() const;  // 秒。ティアが上がるほど短い（＝速く反応する）
	float AimErrorRad() const;    // ラジアン。ティアが上がるほど小さい（＝正確になる）
	float RampT() const;          // 0〜1 に正規化した強化進行度（二次カーブ適用済み）

	//==============================
	// Day5 の対抗行動が読む観測結果
	//==============================
	float PreferredRange() const;          // プレイヤーが保ちがちな間合い
	float CrouchRatio() const;             // 観測時間のうちしゃがんでいた割合 0〜1
	float JumpsPerSecond() const;          // 単位時間あたりのジャンプ回数
	bool  LikesCampingBreakable() const;   // 危うい壊れ床の上に居座りがちか

private:
	static constexpr int   kMaxTier = 10; // 10 ポイント先取なので最大 10 段
	static constexpr float kReactionAtTier0 = 0.32f;   // 序盤はプレイヤーも慣れていないので緩め
	static constexpr float kReactionAtMaxTier = 0.07f;
	static constexpr float kAimErrAtTier0 = 0.15f;     // ラジアン(≒9度)。序盤は結構外す
	static constexpr float kAimErrAtMaxTier = 0.015f;

	// 強化カーブの指数。1.0=線形、2.0=二次(序盤ゆるやか→終盤急に強く)。
	// t = tier/kMaxTier を t^kRampExponent に変換してから補間する。
	static constexpr float kRampExponent = 2.0f;

	// この割合を超えたら「その癖がある」と見なすしきい値。
	static constexpr float kCampingRatioThreshold = 0.35f;

	int tier_ = 0;

	// ---- 観測の生カウンタ ----
	float observedTime_ = 0.0f;
	float rangeSum_ = 0.0f;
	int   rangeSamples_ = 0;

	int   jumpCount_ = 0;
	bool  prevGroundedValid_ = false;
	bool  prevGrounded_ = true;
	float prevPlayerY_ = 0.0f;

	float crouchTime_ = 0.0f;
	float campingTime_ = 0.0f;
};
