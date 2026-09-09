#pragma once

#include <memory>
#include <vector>

#include "Scene.h"
#include "Camera.h"
#include "Vector4.h"
#include "Primitive/PrimitiveInstance.h"
#include "Object3DInstance.h"
#include "Character/Character.h"
#include "Stage/StageGrid.h"
#include "Stage/StageCatalog.h"
#include "Effect/EffectManager.h"
#include "Weapon/ArcingProjectile.h"
#include "Weapon/WeaponPickup.h"
#include "Weapon/FireHazard.h"
#include "AI/EnemyBrain.h"
#include "AI/PlayerModel.h"
#include "Common/CharacterInput.h"
#include "Match/MatchRule.h"
#include "Tutorial/TutorialDirector.h"
#include "Tutorial/TutorialEnemyBrain.h"

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
///   - 場外・HP0の判定、得点・勝敗(MatchRule)、1ポイント毎のランダムステージ切替、
///     ラウンド開始前の3秒カウントダウンもここが面倒を見る(CheckKnockoutAndReset / roundState_ 参照)。
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

	/// <summary>
	/// アトラクト(デモ)モードを有効にする。SceneFactory が "Title" 用の GameScene に対して呼ぶ。
	/// このモードでは:
	///   - ステージは常に Sample 固定(ランダム抽選しない)。
	///   - プレイヤー枠も playerBrain_ が動かす(敵 AI 同士のデモプレイ)。
	///   - セットが決着しても Result へ遷移せず、得点をリセットして無限にループする。
	///   - ESC/(B) でのタイトル復帰は無効。代わりに SPACE/Enter/(A) で本編(Game)へ入る。
	/// Initialize() より前に呼ぶこと。
	/// </summary>
	void SetAttractMode(bool on) { attractMode_ = on; }

	/// <summary>
	/// チュートリアルモードを有効にする。SceneFactory が "Tutorial" 用の GameScene に対して呼ぶ。
	/// このモードでは:
	///   - ステージは Stage_Tutorial 固定(カウントダウン無しで即開始)。
	///   - 敵は tutorialBrain_(攻撃してこない移動専用 AI)が動かす。
	///   - 武器の定期スポーンは止め、説明が武器の段に来たときだけ 1 丁置く。
	///   - 得点(MatchRule)・ステージ切替・Result 遷移は一切行わない。どちらかがやられても
	///     地形の破壊状況と説明の進行段階はそのままに、位置と HP だけ初期へ戻す。
	///   - 全ての説明を終えた後に敵を倒すと、セーブして本編(Game)へ遷移する。
	/// Initialize() より前に呼ぶこと。
	/// </summary>
	void SetTutorialMode(bool on) { tutorialMode_ = on; }

private:
	// アトラクト(デモ)モードか。SetAttractMode 参照。
	bool attractMode_ = false;

	//====================
	// チュートリアル(SetTutorialMode 参照)
	//====================

	bool tutorialMode_ = false;
	// 説明の進行(どの段を出しているか)だけを持つ。描画・完了判定は GameScene 側。
	std::unique_ptr<TutorialDirector> tutorial_;
	// 攻撃してこない移動専用の敵 AI。チュートリアル時のみ生成し、enemyBrain_ の代わりに使う。
	std::unique_ptr<TutorialEnemyBrain> tutorialBrain_;
	// どちらかがやられてから位置リセットするまでの猶予(死亡モーション/落下を少し見せる)。
	float tutorialRespawnTimer_ = 0.0f;
	static constexpr float kTutorialRespawnDelay = 0.8f;
	// 完了して本編へ遷移済みか(遷移フレーム以降に多重で処理しないためのラッチ)。
	bool tutorialFinished_ = false;

	/// <summary>
	/// チュートリアルの進行本体。UpdateBattle から、通常モードの得点判定の代わりに呼ぶ。
	/// 武器の設置・SPACE(パッドは (B))での説明送り・やられたときの位置リセット・
	/// 「全説明後に敵を倒したら完了」の判定をまとめて行う。
	/// </summary>
	void UpdateTutorial(float dt);

	/// <summary>
	/// プレイヤーと敵を初期位置・満タン HP へ戻す。ステージは作り直さない ──
	/// 壊した床・起爆した爆弾などの破壊状況と、説明の進行段階をそのまま維持するため。
	/// </summary>
	void ResetTutorialPositions();

	/// <summary>プレイヤー初期位置に一番近い足場の上に、拾える武器(Pistol)を 1 つ置く。</summary>
	void SpawnTutorialWeapon();

	/// <summary>画面下のチュートリアル説明パネルを描く(Draw から呼ぶ)。</summary>
	void DrawTutorialGuide();

	/// <summary>ゲームパッドが接続されているか(説明のキー表記をパッド用に切り替える判定)。</summary>
	bool IsPadConnected() const;

	/// <summary>起動時 / ラウンド跨ぎで読み込むステージ index を決める。
	/// アトラクト時は名前に "Sample" を含む最初のステージ(無ければ 0)、
	/// 通常時は StageCatalog のシャッフルバッグ抽選(Sample 除外・一巡まで重複なし)。</summary>
	int PickStartStageIndex();

	/// <summary>
	/// brain に self/target・ステージ・最寄り pickup・飛来脅威を詰めた BrainContext を渡して
	/// このフレームの CharacterInput を得る。敵 AI にもアトラクト時のプレイヤー AI にも使う。
	/// </summary>
	CharacterInput DecideAiInput(EnemyBrain& brain, Character& self, Character& target,
		const PlayerModel* model, float dt);

	// Resources/Stages/*.csv の一覧。起動時にランダムで1枚選び、デバッグ ImGui から切り替えられる。
	StageCatalog stageCatalog_;
	int currentStageIndex_ = 0;
	// ImGui から要求されたステージ切り替え先（-1 = 要求なし）。次の Update 先頭で実行する。
	int pendingStageLoad_ = -1;

	/// <summary>
	/// stageCatalog_ の index 番のステージを読み込み直し、プレイヤー・敵をそのステージの
	/// マップチップ初期位置へ戻す。飛翔中の弾・pickup・炎・敵 AI の状態もリセットする。
	/// 本番のステージ遷移でもそのまま呼べる形にしてある。
	/// </summary>
	void LoadStage(int index);

	/// <summary>ベルトコンベア・トゲ・ポータル・爆風をキャラへ適用する（player/enemy の Update 後に呼ぶ）。</summary>
	void UpdateStageGimmicks(float dt);

	/// <summary>
	/// 現在のステージの各ポータル位置に Warp エフェクト（loop）を常駐再生し直す。
	/// 前のステージぶんのハンドルは Stop する。Initialize / LoadStage の末尾で呼ぶ。
	/// </summary>
	void RefreshPortalEffects();

	// 各ポータルで再生中の Warp エフェクトのハンドル（ステージ切り替え時に Stop する）。
	std::vector<EffectHandle> portalEffectHandles_;

	/// <summary>
	/// roundState_ == Battle のときだけ Update() から呼ばれる、通常のゲーム進行本体。
	/// 入力→AI思考→Character::Update→当たり判定→ステージギミック→攻撃判定→弾/武器→
	/// 場外/HP0判定(得点・ステージ切替・勝敗判定)までをすべてここで行う。
	/// </summary>
	/// <param name="playerInput">Update() が生の入力デバイスから組み立てた、プレイヤーの意図。</param>
	void UpdateBattle(float dt, const CharacterInput& playerInput);

	/// <summary>
	/// 中心 center・半サイズ half の AABB が接地している足元セルがベルトコンベアなら、
	/// このフレームで横へ動かすべき量（符号付き）を返す。乗っていなければ 0。
	/// マージン内でしっかり乗っていれば通常速度、端をはみ出したら強めに押し出す。
	/// </summary>
	float BeltShiftX(const Vector3& center, const Vector3& half, float dt) const;

	// ポータルは「セルから出るまで再ワープしない」。今フレーム、キャラがポータルに乗っているか。
	bool playerInPortal_ = false;
	bool enemyInPortal_ = false;

	// 敵がこのフレームにトゲで即死したか。UpdateStageGimmicks で立て、KO 判定で
	// EnemyBrain::NotifyDeath へ渡して消費する（トゲ自滅を AI の慎重さ学習に反映）。
	bool enemyDeathBySpike_ = false;

	// ベルトコンベアに乗っているキャラを毎秒どれだけ横へ流すか。
	static constexpr float kBeltSpeed = 4.0f;
	// 足がベルト端のマージン（kBeltEdgeMargin）を越えて残りわずかしか乗っていないとき、
	// この倍率で押し出して確実に落とす（端で止まってバランスを取らせない）。
	static constexpr float kBeltEdgeMargin = 0.2f;
	static constexpr float kBeltEdgeEjectMul = 3.0f;
	// 爆弾の爆風がキャラを吹き飛ばす初速（爆心で最大、半径の端で0）。
	static constexpr float kBombKnockbackPower = 15.0f;

	// Stage Select ImGui ウィンドウ（プロセス中1回だけ登録）から現在の GameScene を触るための口。
	static GameScene* s_activeForDebug_;

	//====================
	// コントローラー振動（XInput）
	// left  = 低周波の重い振動 / right = 高周波の細かい振動。0〜65535。
	// TriggerRumble で (左右の強さ, 秒数) をセットし、毎フレーム UpdateRumble が
	// 残り時間を減らして 0 で StopVibration する。attractMode_ 中は鳴らさない。
	//====================
	static constexpr unsigned short kHitMotorLeft = 14000;   // 被弾：弱め（左右対称）
	static constexpr unsigned short kHitMotorRight = 22000;
	static constexpr float kHitRumbleSeconds = 0.18f;
	static constexpr unsigned short kExplosionMotorMid = 40000; // 爆発：中くらい（爆発側 / 近距離）
	static constexpr unsigned short kExplosionMotorLow = 16000; // 爆発：弱め（爆発と反対側で振り切ったとき）
	static constexpr float kExplosionRumbleSeconds = 0.35f;
	// 爆発中心とプレイヤーの X 距離がこれ以上なら左右のパンを振り切る（調整用）。
	static constexpr float kRumblePanDistance = 6.0f;

	float rumbleRemaining_ = 0.0f;      // 振動の残り秒（0以下で停止）
	unsigned short rumbleLeft_ = 0;     // 現在鳴らしている左モーター強さ
	unsigned short rumbleRight_ = 0;    // 現在鳴らしている右モーター強さ
	float prevPlayerHP_ = 0.0f;         // 前フレームのプレイヤー HP（減っていたら被弾とみなす）

	/// <summary>
	/// 左右モーターの強さと継続秒を指定して振動を要求する。再生中の振動より弱い要求は
	/// 強さを上書きせず継続時間だけ必要に応じて延長する（強い振動が弱い振動に負けない）。
	/// attractMode_（プレイヤーが AI のデモ）では何もしない。
	/// </summary>
	void TriggerRumble(unsigned short left, unsigned short right, float seconds);

	/// <summary>
	/// 爆発中心 center とプレイヤーの X 座標差から左右モーターの強さを決めて TriggerRumble する。
	/// 爆発側のモーターは常に中、反対側は距離に応じて中→弱へ落ちる（真上・真下・至近は両方中）。
	/// </summary>
	void TriggerExplosionRumble(const Vector3& center);

	/// <summary>振動の残り時間を進め、尽きたら StopVibration する。毎フレーム Update から呼ぶ。</summary>
	void UpdateRumble(float dt);

	/// <summary>
	/// 振動を即時停止して状態をクリアする（安全機能）。
	/// ステージ切り替え・ゲーム終了（シーン遷移）・Finalize で呼び、振動が鳴りっぱなしにならないようにする。
	/// </summary>
	void StopRumble();

	std::unique_ptr<Camera> camera_;

	// アリーナの背景。カメラの奥に大きな Plane を置いてテクスチャを貼るだけで、
	// 当たり判定には関与しない(SpriteInstance だと深度を無視して最前面に出てしまうため
	// 3D の一部として奥へ置く。TitleScene::background_ と同じ理由)。
	std::unique_ptr<PrimitiveInstance> background_;

	// 操作キャラ / 敵キャラ。敵の行動は enemyBrain_ が CharacterInput として決める
	// (Character 側はプレイヤーと敵を区別しない。Character.h の設計コメント参照)。
	std::unique_ptr<Character> player_;
	std::unique_ptr<Character> enemy_;

	// 敵 AI の思考と、プレイヤー行動の学習モデル。
	// turretMode=true にすると敵は「その場で撃つだけの的」に落ちる(Day4 撤退ライン)。
	static constexpr bool kEnemyTurretMode = false;
	std::unique_ptr<EnemyBrain> enemyBrain_;
	std::unique_ptr<PlayerModel> playerModel_;

	// アトラクト(デモ)モードでプレイヤー枠を動かす AI。通常モードでは生成しない。
	std::unique_ptr<EnemyBrain> playerBrain_;

	// アトラクト(デモ)モードで画面上方に表示するタイトルロゴ(Title.mesh)。通常モードでは生成しない。
	std::unique_ptr<Object3DInstance> titleLogo_;
	// タイトルロゴの UV を毎フレーム横スクロールさせる量(0..1 でループ)。虹テクスチャが流れる。
	float titleLogoUvScroll_ = 0.0f;

	// ステージ(CSV から生成)。場外判定・地形当たり判定はここへ委譲する。
	std::unique_ptr<StageGrid> stage_;

	// CSV から読んだスポーン座標(場外/HP0 リセット時の再配置先にも使う)
	Vector3 playerSpawn_{};
	Vector3 enemySpawn_{};

	// 得点・10ポイント先取の勝敗判定(HP0 or 場外で+1、CheckKnockoutAndReset から呼ぶ)。
	MatchRule matchRule_;

	//====================
	// ラウンドの進行状態(Battle → RoundEnd → Countdown → Battle …)
	//====================

	/// <summary>
	/// Battle = 通常のゲーム進行。
	/// RoundEnd = 直前のラウンドの決着直後の猶予(誰が勝ったかを表示しつつ、ゲーム進行は止める)。
	/// Countdown = 次のラウンド開始前の3秒待ち(操作/AI/攻撃/得点判定を止める)。
	/// </summary>
	enum class RoundState {
		Battle,
		RoundEnd,
		Countdown,
	};
	RoundState roundState_ = RoundState::Countdown;
	float countdownRemaining_ = 0.0f;
	static constexpr float kRoundCountdownSeconds = 3.0f;

	/// <summary>roundState_ を Countdown に戻し、kRoundCountdownSeconds 秒からカウントを始める。
	/// ゲーム開始直後(Initialize)と、ステージ切替直後(LoadStage 後)の両方で呼ぶ。</summary>
	void StartRoundCountdown();

	/// <summary>
	/// カウントダウン中の最小限の更新。両キャラは「入力なし」で Update するだけ
	/// (重力・接地・アイドル姿勢は効くが、移動/ジャンプ/攻撃/投げ/AI思考/得点判定は一切行わない)。
	/// 0秒を切ったら roundState_ を Battle に切り替える。
	/// </summary>
	void UpdateRoundCountdown(float dt);

	// 撃破/場外の直後に挟む猶予。「どちらが勝ったか」を表示している間。
	static constexpr float kRoundEndSeconds = 1.5f;
	float roundEndRemaining_ = 0.0f;
	// 猶予明けにやる一度きりの処理(次ステージ抽選 or Result 遷移)を既にやったか。
	bool roundEndActionTaken_ = false;
	// このラウンドで勝った側(Draw() の表示、猶予明けの分岐に使う)。
	MatchRule::Winner roundEndWinnerSide_ = MatchRule::Winner::None;
	// 猶予明けに Result シーンへ遷移すべきか(=このラウンドの得点でセットが決着した)。
	bool roundEndMatchOver_ = false;

	/// <summary>
	/// 猶予中の最小限の更新(内容は UpdateRoundCountdown と同じ、中立入力での Update のみ)。
	/// 0秒を切ったら一度だけ、roundEndMatchOver_ を見て次のランダムステージを予約するか
	/// (pendingStageLoad_)、Result シーンへ遷移する(MatchResultRelay 経由)。
	/// </summary>
	void UpdateRoundEnd(float dt);

	// 秒間隔でステージにランダムな武器を1つ湧かせるまでのカウントダウン。
	static constexpr float kWeaponSpawnInterval = 7.0f;
	// ラウンド開始直後(LoadStage直後)だけは、この短い方の秒数を使う。
	// kWeaponSpawnInterval をそのまま初回にも使うと、戦闘開始からしばらく
	// 誰も武器を拾えない間延びした時間ができてしまうため分けている。
	static constexpr float kInitialWeaponSpawnDelay = 3.0f;
	float weaponSpawnTimer_ = kInitialWeaponSpawnDelay;

	// 飛んでいる銃弾・投げ捨てた武器。どちらも ArcingProjectile で表現する(クラス冒頭コメント参照)。
	std::vector<std::unique_ptr<ArcingProjectile>> flyingObjects_;

	// ステージにタイマーで湧く、その場に静止した拾える武器。
	std::vector<std::unique_ptr<WeaponPickup>> pickups_;

	// 炎銃(FireGun)が着弾点に残す、地面に居座る炎。踏んでいる間、発射者自身を含め
	// 継続ダメージを受け続ける(FireHazard.h の設計コメント参照)。
	std::vector<std::unique_ptr<FireHazard>> fireHazards_;

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
	/// target が HP0 または場外(IsOutOfBounds)になっていないかを見て、なっていれば
	/// matchRule_ 経由で得点を入れ、roundState_ を RoundEnd(誰が勝ったか表示しつつ待つ猶予)に
	/// 切り替える。target はここではリスポーンさせない ── HP0 の死亡モーション(CLIP_DEATH)や
	/// 場外の落下を、猶予中に最後まで見せるため。実際のリセットは次のラウンドへ進むときの
	/// LoadStage に任せる(matchRule_ が決着していれば Result シーンへ遷移するだけでリセット不要)。
	/// 猶予明けの実際の処理は UpdateRoundEnd が行う。何も起きなければ何もしない。
	/// </summary>
	/// <param name="target">場外/HP0をチェックする対象</param>
	/// <param name="otherSide">得点が入る側(target の相手)が Player/Enemy のどちらか</param>
	/// <param name="targetLabel">ログ表示用のラベル("Player"等)</param>
	/// <returns>この呼び出しで撃破/場外が発生し、RoundEnd への切り替えを行ったか。</returns>
	bool CheckKnockoutAndReset(Character& target, MatchRule::Winner otherSide, const char* targetLabel);

	/// <summary>アリーナの左右境界(kArenaHalfExtentX)の外に出ているか。</summary>
	bool IsOutOfBounds(const Vector3& pos) const;

	//====================
	// 武器・弾
	//====================

	/// <summary>
	/// shooter が直前の Update() で銃を発射/武器を投げていれば(ConsumePendingProjectileSpawns /
	/// ConsumePendingThrow)、そのリクエストぶんの ArcingProjectile を生成して flyingObjects_ に積む。
	/// 銃弾と投げ武器は見た目(visualType/visualScale)だけを変えて、同じ経路で生成する。
	/// </summary>
	void SpawnFromCharacter(Character& shooter);

	/// <summary>
	/// flyingObjects_ を1つ生成して積む共通処理。
	/// thrownWeaponPayload が非null(=投げ武器)の場合、着弾時に GameScene 側で
	/// 残弾を見て WeaponPickup として地面に残すかどうかを判断する(UpdateFlyingObjects 参照)。
	/// 銃弾を生成する場合は nullptr のままでよい。
	/// </summary>
	void SpawnFlyingObject(const ProjectileSpawnRequest& spec, Character* owner,
		PrimitiveInstance::PrimitiveType visualType, const Vector3& visualScale, const char* name,
		std::unique_ptr<Weapon> thrownWeaponPayload = nullptr);

	/// <summary>
	/// flyingObjects_ を全て更新し、生きているものは相手キャラとの命中判定を取る。
	/// 消滅したものはリストから取り除く。
	/// </summary>
	void UpdateFlyingObjects(float dt);

	/// <summary>
	/// 爆風(blastRadius > 0)を持つ弾が着弾・消滅したときに呼ぶ。地形を爆風半径ぶんまとめて
	/// 削り、爆心から近いほど強いダメージ/ノックバックを「発射者自身を含む」全キャラクターへ
	/// 与える(ApplyBlastToCharacter に委譲)。通常弾の直撃判定(TryHitCharacter)とは別枠。
	/// </summary>
	void ResolveExplosion(const ArcingProjectile& obj);

	/// <summary>
	/// center を中心とした半径 blastRadius の爆風が target に届いているかを調べ、届いていれば
	/// 距離に応じて減衰させた(爆心=100%、blastRadius の端=0%)ダメージ/ノックバックを
	/// target::ReceiveHit 経由で適用する。target が発射者自身であっても区別なく適用する ──
	/// この「距離が近ければ自分も無事では済まない」という位置関係そのものが、爆風武器の
	/// リスクリワードの正体(Weapon 側に別途「自爆用の反動値」を持たせていない)。
	/// </summary>
	void ApplyBlastToCharacter(Character& target, const Vector3& center, float blastRadius,
		float maxDamage, float maxKnockbackPower);

	/// <summary>
	/// center を中心に半径 radius・DPS dps の炎(FireHazard)を1つ生成し fireHazards_ に積む
	/// (炎銃の着弾点。UpdateFlyingObjects が ArcingProjectile::GetSpawnsFireHazard() を見て呼ぶ)。
	/// </summary>
	void SpawnFireHazard(const Vector3& center, float radius, float duration, float dps);

	/// <summary>
	/// fireHazards_ を全て更新し、寿命が尽きたものを取り除く。生きているものは毎フレーム
	/// player_/enemy_ との重なりを FireHazard::Overlaps() で判定し、重なっていれば
	/// Character::ApplyBurn() を直接呼ぶ(Character::ReceiveHit を経由しない理由は
	/// FireHazard.h の設計コメント参照 ── ノックバックへの意図しない副作用を避けるため)。
	/// </summary>
	void UpdateFireHazards(float dt);

	/// <summary>
	/// character が無武装(CanPickUpWeapon)で pickups_ のいずれかに重なっていれば、
	/// その場で装備させて該当 pickup を消費済みにする。
	/// </summary>
	void TryPickUpWeapon(Character& character);

	/// <summary>weaponSpawnTimer_ を進め、0以下になったらランダムな武器をランダムな位置に1つ湧かせる。</summary>
	void UpdateWeaponSpawner(float dt);
};
