#pragma once

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
	const PlayerModel* playerModel = nullptr; // 反応速度・精度・（Day5）対抗行動の元。null 可
	float dt = 0.0f;
};

/// <summary>
/// 敵 AI の思考。FSM（Idle / Approach / Attack / Retreat）で行動を決め、
/// 出力は CharacterInput 1つだけ。Character にも GameScene のロジックにも触れない
/// （プレイヤー入力と全く同じ経路で Character::Update() に流し込まれる）。
///
/// turretMode: Day4 の撤退ライン用。移動・退避を殺して「その場で撃つだけの的」に落とす。
/// </summary>
class EnemyBrain {
public:
	enum class State { Idle, Approach, Attack, Retreat };

	void Initialize(bool turretMode = false);

	/// <summary>このフレームの行動を返す。</summary>
	CharacterInput Think(const BrainContext& ctx);

	/// <summary>ラウンドリセット時に状態を初期化する（学習ティアは PlayerModel 側が保持）。</summary>
	void ResetForNewRound();

	State GetState() const { return state_; }
	const char* GetStateName() const;

private:
	// ---- 間合い（units）----
	static constexpr float kMeleeRange = 1.6f;       // 素手で殴れる距離
	static constexpr float kEngageRanged = 9.0f;     // 銃ならここまで詰めてから撃つ
	static constexpr float kRangedMax = 12.0f;       // これより遠いと弾が届きにくい
	static constexpr float kRetreatKeepOut = 6.0f;   // 退避中に保ちたい距離
	static constexpr float kTooClose = 3.0f;         // 銃なのに近づかれすぎたら少し引く

	// ---- タイミング ----
	static constexpr float kBaseReactionDelay = 0.35f; // PlayerModel が無いときの反応遅延
	static constexpr float kAttackRefire = 0.15f;      // trigger の最小再発行間隔（単発/素手の取りこぼし防止）
	static constexpr float kJumpInterval = 0.4f;       // 連続ジャンプ入力の間隔

	// ---- ナビの先読み寸法 ----
	static constexpr float kFeetHalfY = 0.9f;   // Character::kRestHeight 相当
	static constexpr float kLookAhead = 1.2f;

	AIPerception perception_;
	State state_ = State::Idle;
	bool turretMode_ = false;

	float reactionTimer_ = 0.0f;
	float attackRefireTimer_ = 0.0f;
	float stateTimer_ = 0.0f;
	float jumpCooldown_ = 0.0f;

	void TransitionTo(State next);
};
