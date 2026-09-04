#pragma once

#include <memory>
#include <string>

#include "Vector3.h"
#include "Primitive/PrimitiveInstance.h"

class Camera;
class Character;

/// <summary>
/// 炎銃(FireGun)が着弾点に残す、地面に居座る炎のハザード。
///
/// WeaponPickup(その場に置いてある拾える武器)と同じ立ち位置 ── GameScene が
/// std::vector<std::unique_ptr<FireHazard>> で管理し、毎フレーム Update() する軽量な実体。
/// ただし WeaponPickup と違って重力落下も武器モデルも持たない。ただ「ここに立っていると燃える」
/// という円形の範囲を、寿命が尽きるまでその場に表示し続けるだけ。
///
/// ダメージの適用方法が Blaster 等の爆風(GameScene::ApplyBlastToCharacter、
/// Character::ReceiveHit 経由)とは意図的に違う点に注意: ReceiveHit は knockbackPower=0 でも
/// Character::ApplyKnockback を呼んでしまい、ApplyKnockback が上書き式なせいで炎の上に
/// 立っているだけで既存のノックバック速度が毎フレーム0にリセットされてしまう副作用がある。
/// それを避けるため、GameScene は毎フレーム Overlaps() で判定してから
/// Character::ApplyBurn() を直接呼ぶ(ノックバック・被弾ヒットボックス判定を一切経由しない)。
/// </summary>
class FireHazard {
public:
	FireHazard();
	~FireHazard();

	/// <param name="position">炎の中心(着弾点)。以後動かない。</param>
	/// <param name="radius">この半径内にいるキャラが対象になる(GameScene::Overlaps の簡易距離判定用)。</param>
	/// <param name="dps">対象へ1秒あたり与える継続ダメージ(Character::ApplyBurn にそのまま渡す)。</param>
	/// <param name="duration">この秒数が経つと自然に消える(IsDead() が true になる)。</param>
	void Initialize(Camera* camera, const Vector3& position, float radius, float dps, float duration);
	void Finalize();

	/// <summary>残り時間を減らすだけ(踏んでいるキャラへの継続ダメージ適用は GameScene 側の責務)。</summary>
	void Update(float dt);
	void Draw();

	/// <summary>寿命が尽きたか。true なら GameScene がリストから除去する。</summary>
	bool IsDead() const { return remaining_ <= 0.0f; }

	Vector3 GetPosition() const { return position_; }
	float GetRadius() const { return radius_; }
	float GetDps() const { return dps_; }

	/// <summary>pos がこの炎の半径内にいるか(2D距離の簡易判定。ArcingProjectile::TryProximityDetonate
	/// と同じ考え方 ── Character のカプセル寸法は private なので、正確なカプセル判定はせずこれで十分とする)。</summary>
	bool Overlaps(const Vector3& pos) const;

private:
	Vector3 position_{};
	float radius_ = 0.0f;
	float dps_ = 0.0f;
	float remaining_ = 0.0f;

	std::unique_ptr<PrimitiveInstance> visual_; // 半径ぶん平たく引き伸ばした箱。地面に置く
};
