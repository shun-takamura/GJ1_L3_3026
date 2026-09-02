#pragma once

#include <memory>
#include <vector>

#include "Scene.h"
#include "Camera.h"
#include "Primitive/PrimitiveInstance.h"
#include "Character/Character.h"
#include "Stage/StageGrid.h"

/// <summary>
/// ゲーム本編の雛形(フェーズ1: 触れる最小プロトタイプ)。
///
/// Stick Fight 系の横視点アクション。カメラは真横遠目の固定視点、
/// キャラの移動は X-Y 平面のみ(奥行き Z は常に固定。両キャラとも Z=0 に置いている)。
///
/// 操作キャラ(player_)と、殴る的として静止した dummy_ を1体置いてある
/// (対戦相手のAIはフェーズ4の別タスクなので、フェーズ1では「静止した的」で代用している)。
///
/// このクラスの役割分担(Character.h の設計コメントと対になっている):
///   - Character 自身は「入力デバイス」も「相手が誰か」も「アリーナの形状」も知らない。
///   - なので、
///       - 入力デバイスを読んで Character::Update() に渡す意図へ変換する(下の Update() 参照)
///       - 攻撃したキャラのヒットボックスを、殴られた側の Character に橋渡しする(ResolveAttack)
///       - HP0 や場外(アリーナの形状に依存する判定)を見て勝敗を決める(CheckKnockoutAndReset)
///     はすべてこの GameScene の責務になる。
///   - 場外・HP0の判定と、仮の得点カウント・その場リセットまではここで面倒を見るが、
///     本物の「10ポイント先取・自動で次ステージへ遷移」といったラウンド進行は
///     フェーズ5で別途実装する(今はテストを続けやすくするための簡易リセットのみ)。
///
/// ESC / (B) でタイトルへ戻る。
/// </summary>
class GameScene : public Scene {
public:
	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override { return camera_.get(); }

private:
	// CSV マップチップ1枚。読み込み・描画・地形当たり判定・破壊・リセットを持つ。
	// Day2 で B の本物の StageGrid に差し替える想定。
	static constexpr const char* kStageCsvPath = "Resources/Stages/Sample_00.csv";

	std::unique_ptr<Camera> camera_;

	// 操作キャラ / 殴る的(dummyは動かない。AIが入るまでの仮の対戦相手役)
	std::unique_ptr<Character> player_;
	std::unique_ptr<Character> dummy_;

	// ステージ(CSV から生成)。場外判定・地形当たり判定はここへ委譲する。
	std::unique_ptr<StageGrid> stage_;

	// CSV から読んだスポーン座標(場外/HP0 リセット時の再配置先にも使う)
	Vector3 playerSpawn_{};
	Vector3 enemySpawn_{};

	// 仮の得点(HP0 or 場外で+1)。本物のポイント管理・10本先取判定はフェーズ5
	int playerPoints_ = 0;
	int dummyPoints_ = 0;

	/// <summary>
	/// attacker が直前の Update() で攻撃していれば(ConsumePendingAttack)、そのヒットボックスを
	/// defender の ReceiveHit() に渡して実際の当たり判定・ダメージ適用まで行わせる。
	/// 命中していればログを出す(挙動確認用)。
	/// </summary>
	void ResolveAttack(Character& attacker, Character& defender, const char* attackerLabel);

	/// <summary>
	/// target が HP0 または場外(IsOutOfBounds)になっていないかを見て、
	/// なっていれば other に1点入れてログを出し、両者をその場でリセットする
	/// (仮リセット。本物のラウンド進行はフェーズ5)。
	/// 何も起きなければ何もしない。
	/// </summary>
	/// <param name="target">場外/HP0をチェックする対象</param>
	/// <param name="other">target をやられたことにした場合、得点が入る側</param>
	/// <param name="otherPoints">other 側の得点カウンタへの参照(加算する)</param>
	/// <param name="targetRespawn">target をリセットするときの再配置先</param>
	/// <param name="targetLabel">ログ表示用のラベル("Player"等)</param>
	void CheckKnockoutAndReset(Character& target, Character& other,
		int& otherPoints, const Vector3& targetRespawn, const char* targetLabel);

	/// <summary>アリーナの左右境界(kArenaHalfExtentX)の外に出ているか。</summary>
	bool IsOutOfBounds(const Vector3& pos) const;
};
