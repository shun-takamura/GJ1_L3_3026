#pragma once

#include <memory>
#include <string>
#include <vector>

#include "IImGuiEditable.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Primitive/PrimitiveInstance.h"
#include "Weapon/ProjectileSpawnRequest.h"

class Camera;
class IStageQuery;
class Weapon;
class Object3DManager;
class Object3DInstance;
class DirectXCore;
class SkinningComputeManager;
class SRVManager;
class AnimatedModelInstance;
class AnimatedObject3DInstance;

/// <summary>
/// 対戦キャラクターの共通実装(プレイヤー・AI 兼用)。
///
/// Stick Fight 系の横視点アクションとして、移動は X-Y 平面のみ
/// (X=左右、Y=重力/ジャンプ)。Z は常に固定(奥行きへは動かない)。
///
/// 設計上の一番大事な約束:
///   - Character は「入力デバイス」も「相手が誰か」も一切知らない。
///     Update() は毎フレーム、すでに解決済みの意図(左右移動量・ジャンプ・
///     しゃがみ・攻撃トリガー)だけを受け取る。
///   - こうしておくことで、プレイヤー入力(GameScene が InputManager から読む)でも
///     AI の行動決定(フェーズ4で追加予定)でも、まったく同じ Update() を呼ぶだけで
///     動かせる。AI 担当は Character の中身を一切変更せずに済む。
///   - 場外判定・勝敗・ラウンド進行・アリーナの形状は GameScene 側の責務。
///     Character はアリーナが四角いのか、床に穴が空いているのかを一切知らない。
///     ステージ担当(フェーズ3)がアリーナ形状を変えても Character は無改造でよい。
///
/// 当たり判定は2種類を使い分けている:
///   - キャラ同士の「押し合い」(すれ違えない)は CollisionSystem に登録した
///     Capsule コライダーが毎フレーム自動で判定してくれる(ResolveBodyBlock 参照)。
///   - 素手攻撃の「殴った/殴られた」は CollisionSystem の総当たりには乗せず、
///     ConsumePendingAttack() / ReceiveHit() で GameScene 側が明示的に橋渡しする
///     (攻撃は一瞬しか判定が要らないため、常時判定する仕組みに乗せる必要がない)。
/// </summary>
class Character : public IImGuiEditable {
public:
	/// <summary>
	/// 素手攻撃1回分のヒットボックス情報。
	///
	/// 流れ: 攻撃した側の Update() 内でこれを作って内部に貯めておき(pendingAttack_)、
	/// GameScene が ConsumePendingAttack() で取り出し、殴られた側の ReceiveHit() に渡す。
	/// Character 同士が直接お互いを呼び合わないようにするための「受け渡し用の小包」。
	///
	/// 武器を実装するフェーズ2では、素手固定だった radius/damage/knockbackPower を
	/// 武器ごとの値に差し替えたバリエーションをここに追加していく想定。
	/// </summary>
	struct AttackHitbox {
		Vector3 center{ 0.0f, 0.0f, 0.0f };  // 判定球の中心(ワールド座標)
		float radius = 0.0f;                 // 判定球の半径
		float damage = 0.0f;                 // 命中時に相手のHPから引く量
		float knockbackPower = 0.0f;         // 命中時に相手へ与える初速の大きさ
		// 攻撃した瞬間の攻撃側の向き(+1=右, -1=左)。ノックバックの方向はここから決める。
		// 「命中位置と防御側の位置関係」から向きを逆算すると、密着距離(お互いのカプセルが
		// めり込むくらい近い)では攻撃ヒットボックスの中心が防御側を追い越してしまい、
		// 符号が反転する(＝攻撃した側に向かって吹っ飛ぶ)バグになる。それを避けるため、
		// 攻撃した瞬間の照準方向(aimDirX_)をそのままコピーして持ち運ぶ。
		float knockbackDirX = 1.0f;

		// ---- 状態異常(氷銃・炎銃用) ----
		// 既定値(倍率1.0/持続0)なら「効果無し」を意味し、既存の全武器の挙動は変わらない。
		// ReceiveHit() が duration>0 のときだけ Character::ApplySlow/ApplyBurn を呼ぶ。
		float slowMultiplier = 1.0f; // 1.0未満で移動速度が遅くなる(0.35なら35%の速さに)
		float slowDuration = 0.0f;   // この秒数だけ遅くなる。0以下なら減速効果無し
		float burnDps = 0.0f;        // 継続ダメージ(1秒あたり)
		float burnDuration = 0.0f;   // この秒数だけ継続ダメージを受ける。0以下なら燃焼効果無し
	};

	Character();
	~Character() override;

	/// <summary>
	/// 見た目(Box の PrimitiveInstance)とコライダー(Capsule)を作って spawnPos に配置する。
	/// </summary>
	void Initialize(Camera* camera, const std::string& name, const Vector3& spawnPos);
	void Finalize();

	/// <summary>
	/// 装備中の武器を3Dモデルで手元に表示するために必要な描画コンテキストを渡す。
	/// GameScene が Initialize 直後に一度呼ぶ(未設定なら手元の武器モデルは表示されないだけ)。
	/// </summary>
	void SetWeaponRenderContext(Object3DManager* object3DManager, DirectXCore* dxCore);

	/// <summary>
	/// 見た目の仮 Box を、スキニング付きアニメモデル(Resources/Models/Player)に差し替える。
	/// GameScene が Initialize 直後に一度呼ぶ。アセット(player.mesh)が無ければ何もせず Box のまま。
	/// teamColor はマテリアルのティント色(プレイヤー=青 / 敵=赤)。被弾中は赤・氷結中は水色に上書きされる。
	/// </summary>
	void SetupAnimatedModel(Object3DManager* object3DManager,
		SkinningComputeManager* skinningComputeManager, DirectXCore* dxCore, SRVManager* srvManager,
		const Vector4& teamColor);

	/// <summary>アニメモデルを使っているか(SetupAnimatedModel が成功したか)。</summary>
	bool HasAnimatedModel() const { return animChara_ != nullptr; }

	/// <summary>アニメモデルのスキニング(Compute)を Dispatch する。GameScene が Draw の先頭で呼ぶ。</summary>
	void DispatchAnimatedSkinning(DirectXCore* dxCore);

	/// <summary>アニメモデルを描画する。GameScene が Object3DManager::DrawSetting 後に呼ぶ。</summary>
	void DrawAnimatedModel(DirectXCore* dxCore);

	/// <summary>
	/// 毎フレーム更新。すべての引数は「もう解決済みの意図」であり、Update 自身は
	/// キーボードやゲームパッドの状態を一切読まない(クラス冒頭のコメント参照)。
	/// </summary>
	/// <param name="dt">経過時間(秒)。GameScene 側で TimeGroup 済みの値を渡す想定。</param>
	/// <param name="moveX">左右の移動量。-1.0〜1.0を想定(アナログ入力の弱倒しはそのままの大きさで渡してよい)。</param>
	/// <param name="jumpTriggered">ジャンプ入力が押された「瞬間」か(接地中のみ実際にジャンプする)。</param>
	/// <param name="crouchHeld">しゃがみ入力が押されている「間」ずっと true。接地中のみ有効で、
	/// しゃがんでいる間は移動・ジャンプができなくなる(見た目も低くなる)。</param>
	/// <param name="aimDirX">照準方向のX成分(マウスカーソル/右スティックから GameScene が計算する)。
	/// 移動方向とは独立していて、素手パンチ・銃の弾道・投げ捨てのすべてがこの方向を向く。</param>
	/// <param name="aimDirY">照準方向のY成分。</param>
	/// <param name="attackTriggered">攻撃入力が押された「瞬間」か(単発武器・素手のクールダウン判定用)。</param>
	/// <param name="attackHeld">攻撃入力が押されている「間」か(アサルトライフルのような連射武器用)。</param>
	/// <param name="throwTriggered">武器投げ捨て入力が押された「瞬間」か(素手のときは何も起きない)。</param>
	void Update(float dt, float moveX, bool jumpTriggered, bool crouchHeld,
		float aimDirX, float aimDirY, bool attackTriggered, bool attackHeld, bool throwTriggered);

	/// <summary>見た目(Box)を描画する。当たり判定のデバッグ描画は CollisionSystem 側が別途行う。</summary>
	void Draw();

	/// <summary>
	/// 装備中の武器の3Dモデルを手元に描画する。GameScene が Object3DManager::DrawSetting 後に呼ぶ
	/// (素手・モデル未設定・コンテキスト未設定なら何もしない)。
	/// </summary>
	void DrawWeaponModel(DirectXCore* dxCore);

	//====================
	// 外部インターフェース
	// 武器(フェーズ2)・AI(フェーズ4)・ステージ(フェーズ3)担当がここだけを見て
	// Character を操作できるようにする窓口。内部の物理パラメータやコライダーの
	// 実装詳細を知らなくても、この関数群だけで「殴る」「吹き飛ばす」「位置を知る」ができる。
	//====================

	/// <summary>HP を amount だけ減らす(0未満にはならない)。</summary>
	void ApplyDamage(float amount);

	/// <summary>directionX の符号方向へ、大きさ power の初速でノックバックさせる(横視点なので水平X方向のみ)。</summary>
	void ApplyKnockback(float directionX, float power);

	/// <summary>
	/// (dirX, dirY) の向き(正規化されていなくてよい)へ大きさ power で吹き飛ばす。
	/// 水平成分は knockbackVelocityX_ を上書き、垂直成分は verticalVelocity_ を上書きし、
	/// 上向きなら接地フラグも外す。爆風の「放射状に吹っ飛ぶ」表現用(ApplyKnockback の斜め版)。
	/// </summary>
	void ApplyBlastKnockback(float dirX, float dirY, float power);

	/// <summary>移動速度に multiplier(1.0未満で減速)を duration 秒だけ掛ける(氷銃用)。
	/// ApplyKnockback と同じ上書き式 ── 再命中すれば効果時間・強さがその時点の値に更新される
	/// (積み増しはしない)。multiplier が 1.0 以上、または duration が 0 以下なら何もしない。</summary>
	void ApplySlow(float multiplier, float duration);

	/// <summary>1秒あたり dps のダメージを duration 秒間、毎フレーム ApplyDamage で与え続ける(炎銃用)。
	/// ApplySlow と同じ上書き式。dps が 0 以下、または duration が 0 以下なら何もしない。</summary>
	void ApplyBurn(float dps, float duration);

	Vector3 GetPosition() const { return position_; }
	void SetPosition(const Vector3& pos) { position_ = pos; }

	/// <summary>
	/// 現在の当たり判定 AABB の中心。しゃがみ中は頭が下がるぶん中心も下へずれる
	/// （Update() 内の MoveAabb に渡している中心・半サイズと同じ計算）。
	/// ステージギミック（トゲ・ポータル）の重なり判定を「プレイヤーのサイズ」で取るために公開している。
	/// </summary>
	Vector3 GetColliderCenter() const;
	/// <summary>現在の当たり判定 AABB の半サイズ。しゃがみ中は高さ(y)が縮む。</summary>
	Vector3 GetColliderHalfExtent() const;

	/// <summary>現在の照準方向(正規化済み)。デバッグ表示(照準レイの描画)用に公開している。</summary>
	float GetAimDirX() const { return aimDirX_; }
	float GetAimDirY() const { return aimDirY_; }

	/// <summary>
	/// 地形問い合わせ先を差し込む。nullptr のままなら「常に y=kRestHeight に平床がある」
	/// という単純化で動く(Character 単体テスト用)。GameScene が生成時に設定する。
	/// </summary>
	void SetStage(const IStageQuery* stage) { stage_ = stage; }
	float GetHP() const { return hp_; }
	float GetMaxHP() const { return kMaxHP; }
	bool IsDead() const { return hp_ <= 0.0f; }

	/// <summary>接地しているか。AI の学習(プレイヤーのジャンプ頻度計測)用に公開。</summary>
	bool IsGrounded() const { return grounded_; }

	/// <summary>
	/// 直前の Update() でジャンプの踏み切りが発生していれば true を返し、フラグを消費する
	/// (接地ジャンプ・コヨーテジャンプが対象)。GameScene が足元にジャンプエフェクトを出すのに使う。
	/// </summary>
	bool ConsumeJumpEffect() { bool v = jumpEffectPending_; jumpEffectPending_ = false; return v; }
	/// <summary>
	/// 直前の Update() で着地の瞬間が発生していれば true を返し、フラグを消費する。
	/// GameScene が足元に着地エフェクトを出すのに使う。
	/// </summary>
	bool ConsumeLandEffect() { bool v = landEffectPending_; landEffectPending_ = false; return v; }
	/// <summary>しゃがみ中か。AI の学習(しゃがみ回避の癖の計測)用に公開。</summary>
	bool IsCrouching() const { return isCrouching_; }

	/// <summary>燃焼(炎銃)状態か。</summary>
	bool IsBurning() const { return burnTimer_ > 0.0f; }
	/// <summary>氷結(氷銃)状態か。</summary>
	bool IsFrozen() const { return slowTimer_ > 0.0f; }

	/// <summary>
	/// 状態異常アウトライン用の ID マスク値。0=無し / 1=炎(赤) / 2=氷(青)。
	/// 両方成立していれば炎を優先する。
	/// </summary>
	uint8_t GetStatusOutlineId() const {
		if (IsBurning()) return 1;
		if (IsFrozen())  return 2;
		return 0;
	}

	/// <summary>
	/// 状態異常アウトライン用の ID パス。状態が非0のとき animChara_ に ID を書き込んで
	/// idMaskRT へシルエットを描く。GameScene が PostEffect の IdPass 内で呼ぶ。
	/// (アニメモデル未生成・状態無しなら何もしない)
	/// </summary>
	void DrawStatusOutlineIdPass(DirectXCore* dxCore);

	/// <summary>今フレームの壁接触方向(-1=左に壁 / +1=右に壁 / 0=なし)。HUD・AI 用に公開。</summary>
	int GetWallContactDir() const { return wallContactDir_; }
	/// <summary>今フレーム壁ずり落ち(落下速度が緩む状態)が働いているか。HUD・AI 用に公開。</summary>
	bool IsWallSliding() const { return wallSliding_; }

	/// <summary>
	/// HPを全回復し、ノックバック速度や落下速度もクリアして spawnPos へ再配置する。
	/// 得点によるその場リセットと、ステージ丸ごと切替(GameScene::LoadStage)の
	/// どちらからも呼ばれる(GameScene::CheckKnockoutAndReset / MatchRule 参照)。
	/// </summary>
	void ResetForNewRound(const Vector3& spawnPos);

	/// <summary>
	/// このキャラが直前の Update() で攻撃を繰り出していれば true を返し、
	/// そのヒットボックス情報を outHitbox に詰める。一度取り出すと消費され、
	/// 次に呼んでも(次に攻撃するまでは) false が返る。
	/// GameScene が毎フレーム両キャラ分呼び出し、当たっていれば相手の ReceiveHit() に渡す。
	/// </summary>
	bool ConsumePendingAttack(AttackHitbox& outHitbox);

	/// <summary>
	/// 相手から受け取った攻撃ヒットボックスを、自分のカプセル形状と実際に判定する。
	/// 当たっていればダメージとノックバックをその場で適用して true を返す。
	/// (「当たったかどうか」の幾何判定も、この関数の中で完結させている)
	/// </summary>
	bool ReceiveHit(const AttackHitbox& hitbox);

	//====================
	// 武器(フェーズ2)
	// Character は「今どの Weapon を1つ持っているか」だけを知っていて、反動・弾道・
	// 連射方式など武器ごとの違いは Weapon 側の責務(Weapon.h の設計コメントと対)。
	//====================

	/// <summary>今の武器を捨てて weapon を装備する。呼び出し側(GameScene)が
	/// CanPickUpWeapon() で無武装であることを確認してから呼ぶ想定。</summary>
	void EquipWeapon(std::unique_ptr<Weapon> weapon);

	/// <summary>今、素手(=ステージの武器を拾える状態)かどうか。</summary>
	bool CanPickUpWeapon() const;

	/// <summary>HUD表示用の、今装備している武器の名前。</summary>
	std::string GetEquippedWeaponName() const;

	/// <summary>HUD表示用の、今装備している武器の残弾数(概念が無ければ Weapon::kInfiniteAmmo)。</summary>
	int GetEquippedAmmo() const;

	/// <summary>
	/// このキャラが直前の Update() で銃を発射していれば true を返し、生成された弾の
	/// リクエスト(ショットガンなら複数)を outSpawns に詰める。ConsumePendingAttack と対になる、
	/// 飛び道具用の受け渡し窓口。GameScene が毎フレーム回収し、実際の Projectile を生成する。
	/// </summary>
	bool ConsumePendingProjectileSpawns(std::vector<ProjectileSpawnRequest>& outSpawns);

	/// <summary>
	/// このキャラが直前の Update() で武器を投げ捨てていれば true を返し、投げた物の
	/// 初期条件(位置・初速・ダメージ等)を outSpawn に、投げた武器そのもの(残弾込み)を
	/// outWeapon に詰める。呼ばれた時点で装備は既に素手に戻っている。
	/// 素手のときに throwTriggered が来ても何も起きない。
	///
	/// outWeapon を渡す理由: 残弾が残っている武器を投げた場合、着弾しても消えずに
	/// その場に WeaponPickup として再配置できるようにするため(GameScene 側の責務)。
	/// 残弾0の武器は GameScene 側で「拾えるものを残さず消す」判断に使われる。
	/// </summary>
	bool ConsumePendingThrow(ProjectileSpawnRequest& outSpawn, std::unique_ptr<Weapon>& outWeapon);

	//====================
	// IImGuiEditable(エンジン側の Hierarchy / Inspector / CollisionSystem への登録に使われる)
	//====================
	std::string GetName() const override { return name_; }
	void SetName(const std::string& name) override { name_ = name; }
	std::string GetTypeName() const override { return "Character"; }
	void OnImGuiInspector() override;
	/// <summary>ギズモ操作・コライダー計算用に position_ そのものへのポインタを返す。</summary>
	Vector3* GetEditableTranslate() override { return &position_; }

private:
	//====================
	// 調整用パラメータ。数値のチューニングはここを触るだけでよい
	// (座標計算やコライダー設定などのロジックには触らない)。
	//====================
	static constexpr float kMaxHP = 100.0f;
	static constexpr float kMoveSpeed = 6.0f;             // 左右移動の速さ(units/秒)
	static constexpr float kGravity = -26.0f;             // 重力加速度(下向きなので負の値)
	static constexpr float kJumpSpeed = 11.0f;             // ジャンプ開始時の上向き初速
	static constexpr float kCoyoteTime = 0.10f;           // 足場を離れた直後、空中でもジャンプできる猶予秒(コヨーテタイム)
	static constexpr float kRestHeight = 0.9f;            // 接地時の position_.y (ボックスの中心の高さ。床が y=0 の前提)
	static constexpr float kCapsuleRadius = 0.45f;        // 当たり判定カプセルの半径
	static constexpr float kCapsuleHeight = 0.9f;         // 当たり判定カプセルの円柱部分の高さ(両端の半球は含まない)
	static constexpr float kKnockbackDamping = 6.0f;      // ノックバック速度の減衰係数。大きいほど速く止まる
	static constexpr float kCrouchHeightScale = 0.5f;     // しゃがみ時、見た目・当たり判定の高さを何倍にするか
	static constexpr float kCrouchMoveScale = 0.5f;       // しゃがみ歩きの速度倍率(通常移動に対して)
	static constexpr float kDamageFlashDuration = 0.15f;  // ダメージを受けたときに見た目を赤くする秒数

	// ---- 壁ジャンプ / 壁ずり落ち パラメータ ----
	static constexpr float kWallJumpUpSpeed = 11.0f;        // 壁ジャンプ時の上向き初速(繰り返すと少しずつ登れる程度に控えめ)
	static constexpr float kWallJumpPushXSpeed = 9.0f;     // 壁ジャンプ時、壁と反対方向へ与える水平初速
	static constexpr float kWallJumpPushDamping = 3.0f;    // 上の水平初速の減衰係数(ノックバックより緩め=しばらく反対へ流れる)
	static constexpr float kWallJumpInputLockTime = 0.30f; // 壁ジャンプ後、壁方向への移動入力を打ち消す秒数(壁との接触を一度切るのに必要な最小限)
	static constexpr float kWallSlideMaxFallSpeed = 6.0f;  // 壁ずり落ち中の落下速度の上限(通常の自由落下より遅くなる)
	static constexpr float kWallProbeReach = 0.08f;        // 体の外側どこまでを「壁に密着」と見なすか
	static constexpr float kWallSlideGroundProbe = 1.0f;   // 足元この距離までブロックが無ければ壁ずり落ちを有効にする

	// ---- 投げ捨てパラメータ ----
	// 「今何を持っているか」に関わらず固定値(残弾ゼロの銃を投げても同じ威力)。
	// 弾道は銃弾と同じ放物線(ArcingProjectile)を使う。
	static constexpr float kThrowSpeed = 12.0f;          // 投げる初速の大きさ
	static constexpr float kThrowDamage = 8.0f;          // 命中時のダメージ
	static constexpr float kThrowKnockbackPower = 9.0f;  // 命中時のノックバック
	static constexpr float kThrowRadius = 0.3f;          // 当たり判定球の半径(武器を模した大きめの弾扱い)
	static constexpr float kThrowGravityScale = 1.0f;    // 重力の掛かり具合
	static constexpr float kThrowLifeTime = 3.0f;        // 何にも当たらなかった場合に消えるまでの秒数
	static constexpr float kThrowForwardOffset = 1.0f;   // 投げる位置を自分の中心からどれだけ照準方向へ離すか
	static constexpr float kThrowWallRestitution = 0.5f; // 投げた武器が壁で滑るように弾かれる程度(床は0=着地即静止)

	// ---- 予備動作(windup) ----
	// アニメの「実際に飛ぶ/投げる/当てる」フレームにゲーム内効果を合わせるための遅延。
	// 値は generate_anims.py の各クリップのコミットフレームに対応させてある。
	static constexpr float kJumpWindup  = 0.15f;  // しゃがみ込み → 踏み切り
	static constexpr float kThrowWindup = 0.17f;  // 振りかぶり → リリース
	static constexpr float kMeleeWindup = 0.15f;  // 引き → 打撃

	/// <summary>CollisionSystem に自分用の Capsule コライダーを設定する(Initialize から呼ぶ)。</summary>
	void SetupCollider();

	/// <summary>
	/// 現在位置の左右に solid セルが密着しているかを stage_->OverlapsSolid で調べる。
	/// 右に壁なら +1、左に壁なら -1、どちらでもなければ 0(右を優先)。
	/// stage_ 未設定なら常に 0(平床フォールバックには壁が無い)。
	/// </summary>
	int DetectWallContact() const;

	/// <summary>
	/// 足元から reach の距離までの間に solid セルがあるか(壁ずり落ちの「下にブロックが無い」判定用)。
	/// stage_ 未設定なら常に true(平床フォールバックでは常に床がある扱い)。
	/// </summary>
	bool HasSolidBelow(float reach) const;

	/// <summary>
	/// 現在の姿勢(立ち/しゃがみ)に応じた当たり判定カプセルの中心・円柱高さ・半径を返す。
	/// しゃがみ中は総高が半分(kCrouchHeightScale)になり、足元は固定で中心が下がる。
	/// 攻撃の被弾判定(ReceiveHit)と、キャラ同士の押し合い(CollisionSystem)の両方で使う。
	/// </summary>
	void GetPoseCapsule(Vector3& outCenter, float& outCylinderHeight, float& outRadius) const;

	/// <summary>GetPoseCapsule の値を CollisionSystem のコライダー(高さ・半径・オフセット)へ反映する。
	/// Update() の末尾で毎フレーム呼ぶ(しゃがみ切り替えに追従させる)。</summary>
	void SyncColliderToPose();

	/// <summary>
	/// 現在の状態(接地・移動・ジャンプ/落下・死亡)から再生すべきアニメクリップを選び、
	/// 変わっていれば animChara_->PlayAnimation する。Update() の末尾で毎フレーム呼ぶ。
	/// クリップは Idle / Run / Jump / Fall / Land の5種のみ(壁ジャンプ等は暫定で Fall/Jump に寄せる)。
	/// </summary>
	void UpdateAnimationState(float dt, float moveX);

	/// <summary>
	/// 自分のコライダーが他のキャラのコライダーと重なったときに呼ばれるコールバック
	/// (SetupCollider で Collider::onCollision に登録する)。
	/// すり抜け防止のため、重なっている間だけ毎フレーム少しずつ横(X方向)へ押し離す。
	/// 相手側も自分自身の onCollision で同じ処理をする(お互いに押し合う)ので、
	/// 1フレームあたりの移動量は控えめにしてある。
	/// </summary>
	void ResolveBodyBlock(IImGuiEditable* other);

	/// <summary>
	/// 装備中の武器に応じて手元の武器モデル(weaponModel_)を作り直し/破棄し、
	/// 照準方向へ向けて位置・回転を更新する。Update() の末尾で毎フレーム呼ぶ。
	/// </summary>
	void UpdateWeaponModel();

	/// <summary>
	/// 頭上のHPバー(背景の板+HP割合ぶんだけ幅が縮む前景の板)の位置・幅・色を
	/// 現在の position_/hp_ に合わせて更新する。Update() の末尾で毎フレーム呼ぶ。
	/// </summary>
	void UpdateHpBar();

	static constexpr float kModelScale = 1.0f;   // アニメモデルの表示スケール(Blender で約1.86m 相当)

	// ---- 頭上HPバー ----
	static constexpr float kHpBarWidth = 1.1f;    // 満タン時の幅
	static constexpr float kHpBarHeight = 0.14f;  // 背景の板の高さ(前景はこれよりひと回り薄く見せる)
	static constexpr float kHpBarYOffset = 1.05f; // position_.y からの高さ(頭の少し上に来る値)
	std::unique_ptr<PrimitiveInstance> hpBarBg_;  // 背景(常に満タン幅・暗い色)
	std::unique_ptr<PrimitiveInstance> hpBarFg_;  // 前景(HP割合ぶんの幅・緑→赤)

	std::string name_;
	Camera* camera_ = nullptr;
	const IStageQuery* stage_ = nullptr; // 地形当たり判定の問い合わせ先(未設定なら平床フォールバック)
	std::unique_ptr<PrimitiveInstance> visual_; // 見た目用のBox。アニメモデルが読めなかったときのフォールバック

	// ---- アニメモデル(見た目の本命。SetupAnimatedModel で生成) ----
	// animModel_(データ)を animChara_(描画実体)より長生きさせる。
	std::unique_ptr<AnimatedModelInstance>    animModel_;
	std::unique_ptr<AnimatedObject3DInstance> animChara_;
	Object3DManager*        object3DManagerForAnim_ = nullptr;
	SkinningComputeManager* skinningComputeManager_ = nullptr;
	DirectXCore*            animDxCore_ = nullptr;
	SRVManager*             srvManager_ = nullptr;
	Vector4 teamColor_{ 1.0f, 1.0f, 1.0f, 1.0f };  // プレイヤー=青 / 敵=赤
	int   currentClipIndex_ = -1;                  // 今 animChara_ が再生中のクリップ index(-1=未設定)
	bool  wasGrounded_ = true;                     // 前フレームの接地状態(着地の瞬間検出用)
	bool  jumpEffectPending_ = false;              // 今フレーム踏み切りが起きた(ConsumeJumpEffect 待ち)
	bool  landEffectPending_ = false;              // 今フレーム着地した(ConsumeLandEffect 待ち)
	float landTimer_ = 0.0f;                       // 0より大きい間は Land クリップを優先
	float hitAnimTimer_ = 0.0f;                    // 被弾のけぞり(Hit)を再生する残り秒(ReceiveHit でセット)
	float wallJumpAnimTimer_ = 0.0f;               // 壁蹴り(WallJump)を再生する残り秒(Update の壁ジャンプ分岐でセット)
	float actionAnimTimer_ = 0.0f;                 // 攻撃/投げの余韻(Shoot/Punch/Throw)を再生する残り秒
	int   actionAnimClip_ = -1;                    // actionAnimTimer_ > 0 の間再生するクリップ index

	// ---- 予備動作(windup)の進行状態 ----
	// 0=なし / 1=ジャンプ / 2=投げ / 3=近接。windupTimer_ が 0 を跨いだフレームで効果を発動する。
	int   windupKind_ = 0;
	float windupTimer_ = 0.0f;
	float windupAimX_ = 1.0f;   // 発動時に使う照準(トリガー時点で確定)
	float windupAimY_ = 0.0f;
	Vector3 windupHitOffset_{ 0.0f, 0.0f, 0.0f }; // 近接: トリガー時の「中心→ヒットボックス中心」の相対
	AttackHitbox windupHitbox_{};                 // 近接: 発動時に詰めるヒットボックスの雛形

	// ---- トランスフォーム・物理状態 ----
	Vector3 position_{ 0.0f, kRestHeight, 0.0f }; // ワールド座標(ボックスの中心)。当たり判定もここを基準にする
	float aimDirX_ = 1.0f;                        // 照準方向(正規化済み)。移動方向とは独立
	float aimDirY_ = 0.0f;                        // マウス/右スティックが未入力のフレームは直前の値を維持する
	float knockbackVelocityX_ = 0.0f;             // ノックバックによる水平速度。時間経過で0へ減衰していく
	float verticalVelocity_ = 0.0f;               // 重力・ジャンプによる垂直速度

	// ---- 壁ジャンプ / 壁ずり落ち 状態 ----
	float wallJumpVelocityX_ = 0.0f;      // 壁ジャンプで壁と反対方向へ与えた水平速度。時間経過で0へ減衰
	float wallJumpInputLockTimer_ = 0.0f; // 0より大きい間、wallJumpLockDir_ 方向への moveX を打ち消す
	int   wallJumpLockDir_ = 0;           // 打ち消す向き(+1=右入力を無効 / -1=左入力を無効)
	int   wallContactDir_ = 0;            // 今フレームの壁接触方向(-1=左に壁 / +1=右に壁 / 0=なし)
	bool  wallSliding_ = false;           // 今フレーム壁ずり落ち(落下速度クランプ)が働いているか

	// ---- 状態異常(氷銃・炎銃用。ApplySlow/ApplyBurn 参照) ----
	float slowMultiplier_ = 1.0f; // 1.0=通常速度。slowTimer_ が尽きたら1.0へ戻る
	float slowTimer_ = 0.0f;      // 0より大きい間だけ slowMultiplier_ が有効
	float burnDps_ = 0.0f;        // 継続ダメージの1秒あたりの量
	float burnTimer_ = 0.0f;      // 0より大きい間だけ毎フレーム burnDps_*dt を ApplyDamage する
	bool grounded_ = true;                        // 地面に接地しているか(falseの間だけジャンプ不可)
	float coyoteTimer_ = 0.0f;                    // 0より大きい間は空中でもジャンプ可(接地で kCoyoteTime に補充、ジャンプ/壁蹴り/爆風で0)
	bool isCrouching_ = false;                    // しゃがみ中か(直前の Update() の crouchHeld && grounded_)

	// ---- 戦闘状態 ----
	float hp_ = kMaxHP;
	bool hasPendingAttack_ = false;      // 今フレーム近接攻撃が成立し、ConsumePendingAttack待ちか
	AttackHitbox pendingAttack_{};       // hasPendingAttack_ が true のときだけ有効な内容
	float damageFlashTimer_ = 0.0f;      // 0より大きい間、見た目を赤く表示する(ApplyDamage で再セット)

	// ---- 武器 ----
	std::unique_ptr<Weapon> equippedWeapon_;                    // 常に何かしらの Weapon を指している(素手も Weapon の一種)
	// 手元に表示する武器モデル。素手やモデルの無い武器のときは nullptr。
	Object3DManager* object3DManager_ = nullptr;                // 武器モデル描画用(SetWeaponRenderContext で注入)
	DirectXCore* weaponModelDxCore_ = nullptr;                  // 同上。モデルのリソース生成に要る
	std::unique_ptr<Object3DInstance> weaponModel_;
	std::string weaponModelKey_;                                // 今 weaponModel_ が表しているモデル("dir/file")。持ち替え検出に使う
	std::vector<ProjectileSpawnRequest> pendingProjectileSpawns_; // 今フレーム発射が成立した弾のリクエスト(ショットガンは複数)
	bool hasPendingThrow_ = false;        // 今フレーム投げ捨てが成立し、ConsumePendingThrow待ちか
	ProjectileSpawnRequest pendingThrow_{}; // hasPendingThrow_ が true のときだけ有効な内容
	std::unique_ptr<Weapon> pendingThrowWeapon_; // 投げた武器そのもの(残弾を保持)。hasPendingThrow_ が true のときだけ有効
};
