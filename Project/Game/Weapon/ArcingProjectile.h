#pragma once

#include <memory>
#include <string>

#include "Vector3.h"
#include "Primitive/PrimitiveInstance.h"
#include "ProjectileSpawnRequest.h"

class Camera;
class IStageQuery;
class Character;

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
/// </summary>
class ArcingProjectile {
public:
	ArcingProjectile();
	~ArcingProjectile();

	/// <param name="spec">初期位置・初速・重力倍率・当たり判定半径・寿命・ダメージ・ノックバックのまとめ</param>
	/// <param name="owner">発射/投擲した本人。命中判定から除外するために覚えておく(自分の弾が自分に当たらないように)</param>
	/// <param name="stage">地形問い合わせ先。nullptr なら地形判定はスキップする(寿命切れでのみ消える)</param>
	/// <param name="visualType">見た目に使うプリミティブの種類(銃弾は小さい球、投げ武器は箱、など呼び出し側が決める)</param>
	/// <param name="visualScale">見た目のスケール</param>
	void Initialize(Camera* camera, const std::string& name, const ProjectileSpawnRequest& spec,
		Character* owner, const IStageQuery* stage,
		PrimitiveInstance::PrimitiveType visualType, const Vector3& visualScale);
	void Finalize();

	void Update(float dt);
	void Draw();

	/// <summary>命中/寿命切れ/地形衝突のいずれかで消滅したか。true になったら GameScene がリストから除去する。</summary>
	bool IsDead() const { return dead_; }

	Vector3 GetPosition() const { return position_; }

	/// <summary>
	/// defender と実際に当たっているかを判定し、当たっていればダメージ・ノックバックを
	/// 適用して自分を消滅させる。当たり判定・ダメージ適用のロジックは Character::ReceiveHit に
	/// そのまま委譲する(同じ判定ロジックをここで二重に持たない)。owner 自身には当てない。
	/// </summary>
	void TryHitCharacter(Character& defender);

private:
	Camera* camera_ = nullptr;
	const IStageQuery* stage_ = nullptr;
	Character* owner_ = nullptr;
	std::unique_ptr<PrimitiveInstance> visual_;

	Vector3 position_{};
	float velocityX_ = 0.0f;
	float velocityY_ = 0.0f;
	float gravityScale_ = 1.0f;
	float radius_ = 0.15f;
	float lifeTimer_ = 0.0f;    // 0以下になると、何にも当たらなくても消える
	float damage_ = 0.0f;
	float knockbackPower_ = 0.0f;
	bool dead_ = false;
};
