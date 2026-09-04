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

	Vector3 GetPosition() const { return position_; }
	Vector3 GetVelocity() const { return { velocityX_, velocityY_, 0.0f }; }
	float GetRadius() const { return radius_; }
	float GetDamage() const { return damage_; }

	/// <summary>発射／投擲した本人（AI が「自分に向かってくる弾か」を判定するのに使う）。</summary>
	const Character* GetOwner() const { return owner_; }

	/// <summary>
	/// defender と実際に当たっているかを判定し、当たっていればダメージ・ノックバックを
	/// 適用して自分を消滅させる。当たり判定・ダメージ適用のロジックは Character::ReceiveHit に
	/// そのまま委譲する(同じ判定ロジックをここで二重に持たない)。owner 自身には当てない。
	/// </summary>
	void TryHitCharacter(Character& defender);

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

	Vector3 position_{};
	float velocityX_ = 0.0f;
	float velocityY_ = 0.0f;
	float gravityScale_ = 1.0f;
	float radius_ = 0.15f;
	float lifeTimer_ = 0.0f;    // 0以下になると、何にも当たらなくても消える
	float damage_ = 0.0f;
	float knockbackPower_ = 0.0f;
	bool dead_ = false;
  
	// 投げた武器そのもの。銃弾(Weapon::TryRangedAttack 由来)では常に nullptr。
	// 投げ武器(Character::ConsumePendingThrow 由来)では、残弾があとで拾い直せるよう
	// 消滅するまでここで所有権を持ち続ける。
	std::unique_ptr<Weapon> thrownWeaponPayload_;

	bool diedOnTerrain_ = false; // 地形/場外で消えたか（キャラ命中・寿命切れと区別する）
};
