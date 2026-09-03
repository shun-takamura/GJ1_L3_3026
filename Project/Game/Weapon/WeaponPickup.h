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
///
/// 見た目は仮の小さい箱(Blenderで武器モデルを用意し Object3DInstance で表示する
/// 試みを一度行ったが、Object3D描画パイプラインの配線でGPUハング(TDR)を起こす
/// 未解決の問題があり、いったんプリミティブ表示に戻してある。Weapon::GetModelDirectory()/
/// GetModelFileName() にモデルの場所は残してあるので、原因が分かったら差し替える)。
/// </summary>
class WeaponPickup {
public:
	WeaponPickup();
	~WeaponPickup();

	void Initialize(Camera* camera, const Vector3& position, std::unique_ptr<Weapon> weapon);
	void Finalize();

	/// <summary>毎フレーム呼ぶ。位置自体は動かないが、WVP計算(カメラ行列の反映)のため呼び続ける必要がある。</summary>
	void Update();
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
