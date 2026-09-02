#pragma once

#include <memory>

#include "Vector3.h"
#include "Primitive/PrimitiveInstance.h"

class Camera;
class Weapon;

/// <summary>
/// ステージにタイマーでランダム湧きする、その場に静止した拾える武器。
///
/// 投げ捨てた武器(ArcingProjectile が放物線を飛んで着弾/命中で消える)とは
/// 完全に別物 ── こちらは誰かが無武装で触れるまでずっとその場に残り続ける
/// 「置いてある武器」で、取得のリスクリワードの核になる。
/// </summary>
class WeaponPickup {
public:
	WeaponPickup();
	~WeaponPickup();

	void Initialize(Camera* camera, const Vector3& position, std::unique_ptr<Weapon> weapon);
	void Finalize();

	void Draw();

	Vector3 GetPosition() const { return position_; }

	/// <summary>誰かに取られて weapon_ が空になっているか。true なら GameScene がリストから除去してよい。</summary>
	bool IsTaken() const { return !weapon_; }

	/// <summary>所有権を呼び出し側(取得したキャラへの EquipWeapon)へ渡す。以降 IsTaken() は true になる。</summary>
	std::unique_ptr<Weapon> TakeWeapon();

private:
	Vector3 position_{};
	std::unique_ptr<Weapon> weapon_;
	std::unique_ptr<PrimitiveInstance> visual_;
};
