#pragma once

#include <memory>
#include <string>

#include "Vector3.h"
#include "Primitive/PrimitiveInstance.h"
#include "ProjectileSpawnRequest.h"

class Camera;
class IStageQuery;
class Character;
class Weapon;
class Object3DManager;
class Object3DInstance;
class DirectXCore;

/// <summary>
/// 銃弾・投げ捨てた武器に共通する「初速+重力で放物線を飛び、キャラに当たるか
/// 地形にぶつかるか寿命が尽きたら消える」実体。
///
/// 銃弾(Weapon::TryRangedAttack が生成)と投げ捨てた武器(Character::ConsumePendingThrow が
/// 生成)は、見た目の形とダメージ/ノックバックの"数値"が違うだけで、飛び方も消え方も
/// 全く同じ物理なので、クラスを分けず ProjectileSpawnRequest の中身の違いだけで
/// 両方を表現する1クラスにまとめてある。
///
/// Character の重力積分(verticalVelocity_ += kGravity * dt)と同じ考え方で
/// 位置を積分するが、これは Character 同士の「押し合い」用の Capsule コライダーとは
/// 無関係な、GameScene が明示的に Update/命中判定を呼ぶだけの軽量な実体
/// (06_Collision.md が言う「CollisionSystem の総当たりに乗せない」パターン)。
///
/// 見た目は、投げ捨てた武器なら実際の武器モデル(Object3DInstance)、銃弾なら
/// プリミティブ(小さい球)。モデル指定が無い/読めない場合はプリミティブへフォールバックする。
/// </summary>
class ArcingProjectile {
public:
	ArcingProjectile();
	~ArcingProjectile();

	/// <param name="spec">初期位置・初速・重力倍率・当たり判定半径・寿命・ダメージ・ノックバックのまとめ</param>
	/// <param name="owner">発射/投擲した本人。命中判定から除外するために覚えておく(自分の弾が自分に当たらないように)</param>
	/// <param name="stage">地形問い合わせ先。nullptr なら地形判定はスキップする(寿命切れでのみ消える)</param>
	/// <param name="visualType">フォールバック時に使うプリミティブの種類(銃弾は小さい球、投げ武器は箱)</param>
	/// <param name="visualScale">フォールバック時の見た目のスケール</param>
	/// <param name="object3DManager">武器モデル描画用の共通マネージャ(投げ武器のときのみ使う。nullptr 可)</param>
	/// <param name="dxCore">モデルのリソース生成に要る(nullptr ならモデルを作らずプリミティブ表示)</param>
	/// <param name="modelDir">武器モデルのディレクトリ("Resources/Models/Pistol" 等)。空なら常にプリミティブ表示</param>
	/// <param name="modelFile">武器モデルのメッシュ名("Pistol.mesh" 等)。modelDir が空なら意味を持たない</param>
	void Initialize(Camera* camera, const std::string& name, const ProjectileSpawnRequest& spec,
		Character* owner, const IStageQuery* stage,
		PrimitiveInstance::PrimitiveType visualType, const Vector3& visualScale,
		Object3DManager* object3DManager = nullptr, DirectXCore* dxCore = nullptr,
		const std::string& modelDir = "", const std::string& modelFile = "");
	void Finalize();

	void Update(float dt);
	void Draw();

	/// <summary>武器モデル(Object3D)を描画する。GameScene が Object3DManager::DrawSetting 後にまとめて呼ぶ。</summary>
	void DrawModel(DirectXCore* dxCore);

	/// <summary>命中/寿命切れ/地形衝突のいずれかで消滅したか。true になったら GameScene がリストから除去する。</summary>
	bool IsDead() const { return dead_; }

	/// <summary>地形（または場外）に当たって消えたか。GameScene が壊れる床を削るのに使う。</summary>
	bool DiedOnTerrain() const { return diedOnTerrain_; }

	/// <summary>
	/// Update() が今フレーム地形/場外との接触を検出したが、まだ本当には死んでいない
	/// (=キャラ命中が同フレーム内で優先される余地を残している)状態か。
	///
	/// 地形との接触で即座に dead_ を立ててしまうと、TryHitCharacter() 冒頭の
	/// 「if (dead_) return;」ガードに阻まれて、同じフレームでキャラにも重なっていた場合に
	/// 命中判定そのものが一切試されなくなる(壁際に立っている相手を狙った弾が、壁への着弾を
	/// 理由に命中判定なしで消えてしまうバグ ── 投げ武器で顕著だが原理上は全弾に共通)。
	/// これを避けるため、地形/場外による死は一旦ここに保留し、GameScene が
	/// TryHitCharacter()/TryProximityDetonate() を試した後に ResolvePendingTerrainDeath() を
	/// 呼んで確定させる(キャラに実際に命中していれば、その時点で dead_ が既に立っている
	/// ので何もしない=キャラ命中が地形死より優先される)。
	/// </summary>
	bool HasPendingTerrainDeath() const { return pendingTerrainDeath_; }

	/// <summary>
	/// GameScene が同フレームの TryHitCharacter()/TryProximityDetonate() を一通り試した後、
	/// 毎フレーム必ず呼ぶ。pendingTerrainDeath_ がまだ残っていれば(=キャラ命中で
	/// 上書きされなかった場合)、ここで初めて dead_ を確定させる。
	/// </summary>
	void ResolvePendingTerrainDeath() {
		if (pendingTerrainDeath_ && !dead_) {
			dead_ = true;
		}
		pendingTerrainDeath_ = false;
	}

	Vector3 GetPosition() const { return position_; }
	Vector3 GetVelocity() const { return { velocityX_, velocityY_, 0.0f }; }
	float GetRadius() const { return radius_; }
	float GetDamage() const { return damage_; }
	float GetKnockbackPower() const { return knockbackPower_; }

	/// <summary>爆風半径(0以下なら爆風を持たない通常弾)。GameScene が着弾時にこれを見て
	/// 通常の直撃判定とは別に ResolveExplosion() を呼ぶかどうかを判断する。</summary>
	float GetBlastRadius() const { return blastRadius_; }

	/// <summary>着弾点に炎(FireHazard)を残すか(炎銃専用)。GameScene が着弾時にこれを見て
	/// SpawnFireHazard() を呼ぶかどうかを判断する。GetBlastRadius() と同じ役割の炎版。</summary>
	bool GetSpawnsFireHazard() const { return spawnsFireHazard_; }
	float GetFireHazardRadius() const { return fireHazardRadius_; }
	float GetFireHazardDuration() const { return fireHazardDuration_; }
	float GetFireHazardDps() const { return fireHazardDps_; }

	/// <summary>発射位置からの飛距離(現在位置とoriginの直線距離)。ダメージ距離減衰(Shotgun等)に使う。</summary>
	float GetTraveledDistance() const;

	/// <summary>発射／投擲した本人（AI が「自分に向かってくる弾か」を判定するのに使う）。</summary>
	const Character* GetOwner() const { return owner_; }

	/// <summary>
	/// defender と実際に当たっているかを判定し、当たっていればダメージ・ノックバックを
	/// 適用して自分を消滅させる。当たり判定・ダメージ適用のロジックは Character::ReceiveHit に
	/// そのまま委譲する(同じ判定ロジックをここで二重に持たない)。owner 自身には当てない。
	/// </summary>
	void TryHitCharacter(Character& defender);

	/// <summary>
	/// proximityRadius_ が有効(>0)なとき、target(発射者以外)がその半径内に入っていれば
	/// 直撃していなくても即座に死亡扱いにする(グレネードランチャーの近接センサー)。
	/// dead_ になった場合、GameScene 側の「死んだから爆発する」パイプラインへそのまま合流する。
	/// 実際に起爆させたら true を返す。
	/// </summary>
	bool TryProximityDetonate(const Character& target);

	/// <summary>
	/// 投げ捨てられた武器そのものを積み荷として持たせる(銃弾用途では呼ばなくてよい)。
	/// 着弾・命中で消滅したあと、GameScene が TakeThrownWeaponPayload() で回収し、
	/// 残弾が残っていればその場に WeaponPickup として再配置する(0発ならそのまま失われる)。
	/// </summary>
	void SetThrownWeaponPayload(std::unique_ptr<Weapon> weapon);

	/// <summary>積み荷の所有権を呼び出し側へ渡す(積み荷が無ければ nullptr)。一度取り出すと空になる。</summary>
	std::unique_ptr<Weapon> TakeThrownWeaponPayload();

private:
	Camera* camera_ = nullptr;
	const IStageQuery* stage_ = nullptr;
	Character* owner_ = nullptr;
	std::unique_ptr<PrimitiveInstance> visual_;      // 銃弾・フォールバック用のプリミティブ
	std::unique_ptr<Object3DInstance> model_;        // 投げ武器の実モデル(読み込めた場合のみ)
	float spinAngle_ = 0.0f;                         // 飛行中の見た目の回転(モデルのみ)

	Vector3 origin_{};          // 発射位置(position_ の初期値のコピー)。飛距離計算専用で以後変わらない
	Vector3 position_{};
	float velocityX_ = 0.0f;
	float velocityY_ = 0.0f;
	float gravityScale_ = 1.0f;
	float radius_ = 0.15f;
	float lifeTimer_ = 0.0f;    // 0以下になると、何にも当たらなくても消える
	float damage_ = 0.0f;
	float knockbackPower_ = 0.0f;
	float blastRadius_ = 0.0f; // 0以下なら爆風なし(通常弾)。ProjectileSpawnRequest::blastRadius から複製する
	bool dead_ = false;
	// 地形/場外による死を一旦保留するフラグ(HasPendingTerrainDeath/ResolvePendingTerrainDeath参照)。
	// 寿命切れ(lifeTimer_<=0)による死はこの保留を経由せず即座に dead_ を立てる ──
	// その場合は今フレーム位置が動いていない(Update()の最初でreturnする)ので、キャラ命中を
	// 保留してまで再判定する意味が無い(前フレームまでに既に判定済みの位置と同じため)。
	bool pendingTerrainDeath_ = false;

	// ---- 状態異常(ProjectileSpawnRequest::slowMultiplier等のコピー。詳細はそちらのコメント参照) ----
	float slowMultiplier_ = 1.0f;
	float slowDuration_ = 0.0f;
	float burnDps_ = 0.0f;
	float burnDuration_ = 0.0f;

	// ---- 着弾点を燃やす(ProjectileSpawnRequest::spawnsFireHazard等のコピー) ----
	bool spawnsFireHazard_ = false;
	float fireHazardRadius_ = 0.0f;
	float fireHazardDuration_ = 0.0f;
	float fireHazardDps_ = 0.0f;

	// ---- 跳ね返り(ProjectileSpawnRequest::bounces 系のコピー。詳細はそちらのコメント参照) ----
	bool bounces_ = false;
	float wallRestitution_ = 0.0f;
	float floorRestitution_ = 0.0f;
	int maxBounces_ = 0;   // 0 = 無制限。ProjectileSpawnRequest::maxBounces のコメント参照
	int bounceCount_ = 0;  // 実際に反射した回数(壁・床とも同じカウンタで数える)
	float proximityRadius_ = 0.0f;
	float damageFalloffRange_ = 0.0f;
	float minDamageMultiplier_ = 1.0f;
  
	// 投げた武器そのもの。銃弾(Weapon::TryRangedAttack 由来)では常に nullptr。
	// 投げ武器(Character::ConsumePendingThrow 由来)では、残弾があとで拾い直せるよう
	// 消滅するまでここで所有権を持ち続ける。
	std::unique_ptr<Weapon> thrownWeaponPayload_;

	bool diedOnTerrain_ = false; // 地形/場外で消えたか（キャラ命中・寿命切れと区別する）
};
