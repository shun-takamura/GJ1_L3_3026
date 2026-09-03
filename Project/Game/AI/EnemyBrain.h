#pragma once

#include "Vector3.h"
#include "Common/CharacterInput.h"
#include "AI/AIPerception.h"

class Character;
class IStageQuery;
class PlayerModel;

/// <summary>EnemyBrain::Think() に毎フレーム渡す文脈。</summary>
struct BrainContext {
	const Character* self = nullptr;    // 操作対象の AI キャラ（読み取りのみ）
	const Character* target = nullptr;  // 攻撃対象（＝プレイヤー）
	const IStageQuery* stage = nullptr;
	const PlayerModel* playerModel = nullptr;    // 反応速度・精度・対抗行動の元。null 可
	const Vector3* nearestPickup = nullptr;      // 取得可能で最寄りの武器 pickup 座標。無ければ null
	float dt = 0.0f;
};

/// <summary>
/// 敵 AI の思考。FSM（Idle / Approach / Attack / Retreat / FetchWeapon）で行動を決め、
/// 出力は CharacterInput 1つだけ。Character にも GameScene のロジックにも触れない
/// （プレイヤー入力と全く同じ経路で Character::Update() に流し込まれる）。
///
/// PlayerModel を通じて「学習」した癖への対抗:
///   - しゃがみ多用     → 足元を狙う・距離を潰す・武器を投げる（山なり弾）
///   - 壊れ床に居座る   → その足場を撃って落とす
///   - 移動しながら戦う → 進行方向へ置き撃ち（リード射撃）
///   - ティアが上がる   → 反応が速く・エイムが正確に
///
/// turretMode: Day4 の撤退ライン用。移動・退避を殺して「その場で撃つだけの的」に落とす。
/// </summary>
class EnemyBrain {
public:
	enum class State { Idle, Approach, Attack, Retreat, FetchWeapon };

	void Initialize(bool turretMode = false);

	/// <summary>このフレームの行動を返す。</summary>
	CharacterInput Think(const BrainContext& ctx);

	/// <summary>ラウンドリセット時に状態を初期化する（学習ティアは PlayerModel 側が保持）。</summary>
	void ResetForNewRound();

	/// <summary>
	/// 自分がやられた瞬間に GameScene が呼ぶ。場外(自滅)なら「穴に慎重になる」学習が進む。
	/// この学習はラウンドを跨いで蓄積し、ResetForNewRound では消えない。
	/// </summary>
	void NotifyDeath(bool wasOutOfBounds);

	State GetState() const { return state_; }
	const char* GetStateName() const;
	int GetFallCaution() const { return selfFallCount_; }

	/// <summary>HUD で「なぜ止まっているか」を見るためのデバッグ値。</summary>
	struct Debug {
		float moveX = 0.0f;
		float edgeBias = 0.0f;
		bool terrainBlocked = false;
		bool wantFetch = false;
		bool blacklisted = false;
		float pickupDist = -1.0f;
		float frozen = 0.0f;
	};
	Debug GetDebug() const { return dbg_; }

private:
	// ---- 間合い（units）----
	static constexpr float kMeleeRange = 1.6f;
	static constexpr float kEngageRanged = 11.0f;      // 早めに戦闘モードへ
	static constexpr float kRangedMax = 12.0f;
	static constexpr float kPreferredShootDist = 5.0f; // これより遠ければ撃ちながら前進する
	static constexpr float kRetreatKeepOut = 6.0f;
	static constexpr float kTooClose = 2.0f;           // これ未満でだけ後退（良い位置を捨てにくく）
	static constexpr float kRetreatDuration = 1.5f;    // 撤退状態を続ける最短秒（短め＝粘る）

	// ---- タイミング ----
	static constexpr float kBaseReactionDelay = 0.35f;
	static constexpr float kAttackRefire = 0.15f;
	static constexpr float kJumpInterval = 0.4f;

	// ---- ナビの先読み寸法 ----
	static constexpr float kFeetHalfY = 0.9f;
	static constexpr float kLookAhead = 1.2f;
	static constexpr float kAiMaxJumpGap = 3.0f; // 確実に跳べる幅だけ。穴に落ちるより届かず止まるほうがマシ
	static constexpr float kAiMaxJumpUp = 1.8f;
	static constexpr float kMaxSafeDrop = 6.0f; // これ以内の落差なら「飛び降りて追う／取りに行く」

	// 自滅(場外落下)を繰り返すほど穴に慎重になる。跳べる幅を狭め、崖の検知距離を広げる。
	static constexpr int kMaxFallCaution = 4;

	// ---- 序盤の見切り発車演出（控えめに: 1本取られるまで、幅も少しだけ盛る） ----
	static constexpr int   kRecklessUntilTier = 1;
	static constexpr float kRecklessJumpGapMul = 1.35f;

	// ---- 崖を避けるポジション取り ----
	static constexpr float kEdgeCaution = 2.5f; // 自分の左右この距離に落下があれば崖側移動を止める

	// ---- 動きのコミット（プルプル防止）----
	static constexpr float kMoveCommitTime = 0.22f;  // 動き出した向きを最低この秒数キープ
	static constexpr float kFetchCommitTime = 1.5f;  // 武器を取りに行くと決めたら往復しない秒数

	// ---- Day5: 置き撃ち・エイム誤差 ----
	static constexpr float kBulletSpeedGuess = 16.0f; // リード量の概算に使う弾速
	static constexpr float kMaxLeadTime = 0.6f;       // 先読みしすぎない上限（秒）
	static constexpr float kAimErrorRefresh = 0.3f;   // エイム誤差を引き直す間隔

	// ---- Day5: 学習対抗行動 ----
	static constexpr float kCrouchCounterRatio = 0.25f; // しゃがみ割合がこれ超で対抗
	static constexpr float kFloorAimDrop = 1.2f;        // 壊れ床狙いで相手中心から下げる量
	static constexpr float kThrowVsCroucherCd = 2.5f;   // 対しゃがみの武器投げ間隔

	// ---- Day5: 武器拾い（積極的に）----
	static constexpr float kFetchMaxDist = 22.0f;  // ステージ端から端でも取りに行く
	static constexpr float kFetchSafeDist = 3.0f;  // 殴られる距離でだけ拾いを諦めて戦う
	static constexpr int   kLowAmmoCount = 3;      // 残弾これ以下で予備の武器を意識する
	static constexpr float kThrowRange = 8.5f;     // この距離以内なら弾切れの銃を投げつける

	// ---- Day5: 破綻潰し ----
	static constexpr float kLoSLostTimeout = 1.0f;   // Attack で射線を失い続けたら Approach へ
	static constexpr float kStuckTime = 0.4f;        // 動こうとして動けない時間
	static constexpr float kStuckReverseTime = 0.3f; // 詰まったとき逆方向に下がる時間
	static constexpr float kStuckMoveEps = 0.03f;    // このフレーム移動量がこれ未満なら動けていない

	AIPerception perception_;
	State state_ = State::Idle;
	bool turretMode_ = false;

	float reactionTimer_ = 0.0f;
	float attackRefireTimer_ = 0.0f;
	float stateTimer_ = 0.0f;
	float jumpCooldown_ = 0.0f;

	// ターゲット速度推定（置き撃ち用）
	Vector3 prevTargetPos_{};
	bool prevTargetValid_ = false;
	float targetVelX_ = 0.0f;

	// エイム誤差（一定間隔で引き直す。フレーム毎にジッターさせない）
	float aimErrorCurr_ = 0.0f;
	float aimErrorTimer_ = 0.0f;

	// 破綻潰し
	float losLostTimer_ = 0.0f;
	float prevSelfX_ = 0.0f;
	bool prevSelfValid_ = false;
	float stuckTimer_ = 0.0f;
	float reverseTimer_ = 0.0f;

	float throwCdTimer_ = 0.0f;

	// 移動コミット（プルプル防止）
	float moveCommitTimer_ = 0.0f;
	float committedMoveDir_ = 0.0f;

	// 武器拾いの行き詰まり検知（届かない pickup を追い続けてループするのを防ぐ）
	float fetchBestDist_ = 0.0f;       // FetchWeapon 中に縮められた最短距離
	float fetchStallTimer_ = 0.0f;     // 距離が縮まらないまま経った時間
	float pickupBlacklistTimer_ = 0.0f; // この間、下の x 付近の pickup は無視する
	float pickupBlacklistX_ = 0.0f;

	float terrainBlockedTimer_ = 0.0f; // 地形で足止めされ続けている時間（一定超で「探索」をやめて待つ）

	int selfFallCount_ = 0; // 場外落下で自滅した回数。ラウンドを跨いで蓄積（穴への慎重さ）

	float frozenTimer_ = 0.0f; // 何も出力できていない時間。一定超で強制的に行動させる
	Debug dbg_;

	void TransitionTo(State next);
};
