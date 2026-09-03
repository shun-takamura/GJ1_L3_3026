#pragma once

#include <memory>
#include <vector>

#include "Scene.h"
#include "Camera.h"
#include "Vector4.h"
#include "Primitive/PrimitiveInstance.h"
#include "Character/Character.h"
#include "Stage/StageGrid.h"
#include "Weapon/ArcingProjectile.h"
#include "Weapon/WeaponPickup.h"
#include "AI/EnemyBrain.h"
#include "AI/PlayerModel.h"
#include "Common/CharacterInput.h"

/// <summary>
/// ゲーム本編の雛形(フェーズ1: 触れる最小プロトタイプ)。
///
/// Stick Fight 系の横視点アクション。カメラは真横遠目の固定視点、
/// キャラの移動は X-Y 平面のみ(奥行き Z は常に固定。両キャラとも Z=0 に置いている)。
///
/// 操作キャラ(player_)と、EnemyBrain が動かす敵(enemy_)を1体ずつ置いている。
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

	// 操作キャラ / 敵キャラ。敵の行動は enemyBrain_ が CharacterInput として決める
	// (Character 側はプレイヤーと敵を区別しない。Character.h の設計コメント参照)。
	std::unique_ptr<Character> player_;
	std::unique_ptr<Character> enemy_;

	// 敵 AI の思考と、プレイヤー行動の学習モデル。
	// turretMode=true にすると敵は「その場で撃つだけの的」に落ちる(Day4 撤退ライン)。
	static constexpr bool kEnemyTurretMode = false;
	std::unique_ptr<EnemyBrain> enemyBrain_;
	std::unique_ptr<PlayerModel> playerModel_;

	// ステージ(CSV から生成)。場外判定・地形当たり判定はここへ委譲する。
	std::unique_ptr<StageGrid> stage_;

	// CSV から読んだスポーン座標(場外/HP0 リセット時の再配置先にも使う)
	Vector3 playerSpawn_{};
	Vector3 enemySpawn_{};

	// 仮の得点(HP0 or 場外で+1)。本物のポイント管理・10本先取判定はフェーズ5
	int playerPoints_ = 0;
	int enemyPoints_ = 0;

	// 秒間隔でステージにランダムな武器を1つ湧かせるまでのカウントダウン。
	static constexpr float kWeaponSpawnInterval = 8.0f;
	float weaponSpawnTimer_ = kWeaponSpawnInterval;

	// 飛んでいる銃弾・投げ捨てた武器。どちらも ArcingProjectile で表現する(クラス冒頭コメント参照)。
	std::vector<std::unique_ptr<ArcingProjectile>> flyingObjects_;

	// ステージにタイマーで湧く、その場に静止した拾える武器。
	std::vector<std::unique_ptr<WeaponPickup>> pickups_;

	//====================
	// デバッグ表示(攻撃判定・照準がどこを向いているかを目視確認するため)
	//====================

	/// <summary>一定時間だけ表示され続ける、当たり判定確認用の球ワイヤーフレーム。</summary>
	struct DebugFlash {
		Vector3 position{};
		float radius = 0.0f;
		Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
		float remaining = 0.0f; // 0以下になったら消える
	};
	static constexpr float kDebugFlashDuration = 0.25f; // 攻撃判定は一瞬だけなので、少し残して見えるようにする
	std::vector<DebugFlash> debugFlashes_;

	/// <summary>照準計算(マウスの逆投影)が実際にどのワールド座標を指しているか。毎フレーム更新し、デバッグ描画に使う。</summary>
	Vector3 lastAimWorldPoint_{};

	/// <summary>pos を中心とした半径 radius の球を duration 秒だけデバッグ表示する。</summary>
	void AddDebugFlash(const Vector3& pos, float radius, const Vector4& color, float duration = kDebugFlashDuration);

	/// <summary>debugFlashes_ の残り時間を進め、尽きたものを取り除く。</summary>
	void UpdateDebugFlashes(float dt);

	/// <summary>debugFlashes_ と、プレイヤーの照準方向(レイ+着弾点)をデバッグ描画する。</summary>
	void DrawDebugAids();

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
	/// <returns>この呼び出しで撃破/場外が発生し、リセットを行ったか。</returns>
	bool CheckKnockoutAndReset(Character& target, Character& other,
		int& otherPoints, const Vector3& targetRespawn, const char* targetLabel);

	/// <summary>アリーナの左右境界(kArenaHalfExtentX)の外に出ているか。</summary>
	bool IsOutOfBounds(const Vector3& pos) const;

	//====================
	// 武器・弾(フェーズ2)
	//====================

	/// <summary>
	/// shooter が直前の Update() で銃を発射/武器を投げていれば(ConsumePendingProjectileSpawns /
	/// ConsumePendingThrow)、そのリクエストぶんの ArcingProjectile を生成して flyingObjects_ に積む。
	/// 銃弾と投げ武器は見た目(visualType/visualScale)だけを変えて、同じ経路で生成する。
	/// </summary>
	void SpawnFromCharacter(Character& shooter);

	/// <summary>flyingObjects_ を1つ生成して積む共通処理。</summary>
	void SpawnFlyingObject(const ProjectileSpawnRequest& spec, Character* owner,
		PrimitiveInstance::PrimitiveType visualType, const Vector3& visualScale, const char* name);

	/// <summary>
	/// flyingObjects_ を全て更新し、生きているものは相手キャラとの命中判定を取る。
	/// 消滅したものはリストから取り除く。
	/// </summary>
	void UpdateFlyingObjects(float dt);

	/// <summary>
	/// character が無武装(CanPickUpWeapon)で pickups_ のいずれかに重なっていれば、
	/// その場で装備させて該当 pickup を消費済みにする。
	/// </summary>
	void TryPickUpWeapon(Character& character);

	/// <summary>weaponSpawnTimer_ を進め、0以下になったらランダムな武器をランダムな位置に1つ湧かせる。</summary>
	void UpdateWeaponSpawner(float dt);
};
