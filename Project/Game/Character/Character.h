#pragma once

#include <memory>
#include <string>

#include "IImGuiEditable.h"
#include "Vector3.h"
#include "Primitive/PrimitiveInstance.h"

class Camera;

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
		// 攻撃した瞬間の facingX_ をそのままコピーして持ち運ぶ。
		float knockbackDirX = 1.0f;
	};

	Character();
	~Character() override;

	/// <summary>
	/// 見た目(Box の PrimitiveInstance)とコライダー(Capsule)を作って spawnPos に配置する。
	/// </summary>
	void Initialize(Camera* camera, const std::string& name, const Vector3& spawnPos);
	void Finalize();

	/// <summary>
	/// 毎フレーム更新。すべての引数は「もう解決済みの意図」であり、Update 自身は
	/// キーボードやゲームパッドの状態を一切読まない(クラス冒頭のコメント参照)。
	/// </summary>
	/// <param name="dt">経過時間(秒)。GameScene 側で TimeGroup 済みの値を渡す想定。</param>
	/// <param name="moveX">左右の移動量。-1.0〜1.0を想定(アナログ入力の弱倒しはそのままの大きさで渡してよい)。</param>
	/// <param name="jumpTriggered">ジャンプ入力が押された「瞬間」か(接地中のみ実際にジャンプする)。</param>
	/// <param name="crouchHeld">しゃがみ入力が押されている「間」ずっと true。接地中のみ有効で、
	/// しゃがんでいる間は移動・ジャンプができなくなる(見た目も低くなる)。</param>
	/// <param name="attackTriggered">攻撃入力が押された「瞬間」か(クールダウン中は無視される)。</param>
	void Update(float dt, float moveX, bool jumpTriggered, bool crouchHeld, bool attackTriggered);

	/// <summary>見た目(Box)を描画する。当たり判定のデバッグ描画は CollisionSystem 側が別途行う。</summary>
	void Draw();

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

	Vector3 GetPosition() const { return position_; }
	void SetPosition(const Vector3& pos) { position_ = pos; }
	float GetHP() const { return hp_; }
	float GetMaxHP() const { return kMaxHP; }
	bool IsDead() const { return hp_ <= 0.0f; }

	/// <summary>
	/// HPを全回復し、ノックバック速度や落下速度もクリアして spawnPos へ再配置する。
	/// あくまで「その場でテストを続けられるようにするための仮リセット」であり、
	/// セット10ポイント先取や次ステージ選出といった本物のラウンド進行はフェーズ5の別タスク。
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
	static constexpr float kGravity = -20.0f;             // 重力加速度(下向きなので負の値)
	static constexpr float kJumpSpeed = 8.0f;             // ジャンプ開始時の上向き初速
	static constexpr float kRestHeight = 0.9f;            // 接地時の position_.y (ボックスの中心の高さ。床が y=0 の前提)
	static constexpr float kCapsuleRadius = 0.45f;        // 当たり判定カプセルの半径
	static constexpr float kCapsuleHeight = 0.9f;         // 当たり判定カプセルの円柱部分の高さ(両端の半球は含まない)
	static constexpr float kAttackForwardOffset = 1.0f;   // 攻撃判定球を、自分の位置から前方(facingX_ 方向)へどれだけ離すか
	static constexpr float kAttackRadius = 0.8f;          // 攻撃判定球の半径
	static constexpr float kAttackDamage = 15.0f;         // 素手攻撃1発分のダメージ
	static constexpr float kAttackKnockbackPower = 10.0f; // 素手攻撃命中時のノックバック初速
	static constexpr float kAttackCooldown = 0.5f;        // 攻撃後、次の攻撃が出せるようになるまでの秒数
	static constexpr float kKnockbackDamping = 6.0f;      // ノックバック速度の減衰係数。大きいほど速く止まる
	static constexpr float kCrouchHeightScale = 0.5f;     // しゃがみ時、見た目の高さを何倍にするか

	/// <summary>CollisionSystem に自分用の Capsule コライダーを設定する(Initialize から呼ぶ)。</summary>
	void SetupCollider();

	/// <summary>
	/// 自分のコライダーが他のキャラのコライダーと重なったときに呼ばれるコールバック
	/// (SetupCollider で Collider::onCollision に登録する)。
	/// すり抜け防止のため、重なっている間だけ毎フレーム少しずつ横(X方向)へ押し離す。
	/// 相手側も自分自身の onCollision で同じ処理をする(お互いに押し合う)ので、
	/// 1フレームあたりの移動量は控えめにしてある。
	/// </summary>
	void ResolveBodyBlock(IImGuiEditable* other);

	std::string name_;
	Camera* camera_ = nullptr;
	std::unique_ptr<PrimitiveInstance> visual_; // 見た目用のBox。当たり判定(Capsule)とは別物

	// ---- トランスフォーム・物理状態 ----
	Vector3 position_{ 0.0f, kRestHeight, 0.0f }; // ワールド座標(ボックスの中心)。当たり判定もここを基準にする
	float facingX_ = 1.0f;                        // 現在の向き。+1=右向き, -1=左向き。攻撃の前方判定に使う
	float knockbackVelocityX_ = 0.0f;             // ノックバックによる水平速度。時間経過で0へ減衰していく
	float verticalVelocity_ = 0.0f;               // 重力・ジャンプによる垂直速度
	bool grounded_ = true;                        // 地面に接地しているか(falseの間だけジャンプ不可)
	bool isCrouching_ = false;                    // しゃがみ中か(直前の Update() の crouchHeld && grounded_)

	// ---- 戦闘状態 ----
	float hp_ = kMaxHP;
	float attackCooldownTimer_ = 0.0f;   // 0以下になるまで次の攻撃を出せない
	bool hasPendingAttack_ = false;      // 今フレーム攻撃が成立し、ConsumePendingAttack待ちか
	AttackHitbox pendingAttack_{};       // hasPendingAttack_ が true のときだけ有効な内容
};
