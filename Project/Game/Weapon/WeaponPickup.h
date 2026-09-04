#pragma once

#include <memory>
#include <string>

#include "Vector3.h"
#include "Primitive/PrimitiveInstance.h"

class Camera;
class Weapon;
class IStageQuery;
class Object3DManager;
class Object3DInstance;
class DirectXCore;

/// <summary>
/// ステージにタイマーでランダム湧きする、その場に静止した拾える武器。
///
/// 投げ捨てた武器(ArcingProjectile が放物線を飛んで着弾/命中で消える)とは
/// 完全に別物 ── こちらは誰かが無武装で触れるまでずっとその場に残り続ける
/// 「置いてある武器」で、取得のリスクリワードの核になる。
///
/// 見た目は Weapon::GetModelDirectory()/GetModelFileName() が指す実際の武器モデル
/// (Object3DInstance)。モデルを持たない武器や読み込みに失敗した場合だけ、
/// 仮の小さい箱(PrimitiveInstance)にフォールバックする。
/// </summary>
class WeaponPickup {
public:
	WeaponPickup();
	~WeaponPickup();

	/// <param name="object3DManager">武器モデル描画に使う共通マネージャ(GameScene が Scene 基底から渡す)。</param>
	/// <param name="dxCore">同上。モデルのリソース生成に要る。</param>
	void Initialize(Camera* camera, Object3DManager* object3DManager, DirectXCore* dxCore,
		const Vector3& position, std::unique_ptr<Weapon> weapon, const IStageQuery* stage);
	void Finalize();

	/// <summary>毎フレーム呼ぶ。位置自体は動かないが、WVP計算(カメラ行列の反映)のため呼び続ける必要がある。</summary>
	void Update();

	/// <summary>フォールバックの箱(プリミティブ)だけを描画する。武器モデルは DrawModel() で別途描く。</summary>
	void Draw();

	/// <summary>武器モデル(Object3D)を描画する。GameScene が Object3DManager::DrawSetting 後にまとめて呼ぶ。</summary>
	void DrawModel(DirectXCore* dxCore);

	Vector3 GetPosition() const { return position_; }

	/// <summary>誰かに取られて weapon_ が空になっているか。true なら GameScene がリストから除去してよい。</summary>
	bool IsTaken() const { return !weapon_; }

	/// <summary>所有権を呼び出し側(取得したキャラへの EquipWeapon)へ渡す。以降 IsTaken() は true になる。</summary>
	std::unique_ptr<Weapon> TakeWeapon();

private:
	// 落下シミュレーション用の当たり判定半サイズ(見た目のBoxスケール{0.5,0.3,0.5}の半分)。
	// Character::MoveAabb と同じ仕組みを再利用するための、この見た目に合わせたAABBサイズ。
	static constexpr Vector3 kHalfExtent{ 0.25f, 0.15f, 0.25f };
	static constexpr float kGravity = -20.0f; // Character ほど速く落ちなくてよいので少し緩め

	Vector3 position_{};
	float spinAngle_ = 0.0f; // 置いてある武器がゆっくり回って目を引くための Y 回転(見た目だけ)
  
	std::unique_ptr<Weapon> weapon_;
	std::unique_ptr<Object3DInstance> model_;   // 実際の武器モデル。読み込めた場合はこちらを描く
	std::unique_ptr<PrimitiveInstance> visual_; // モデルが無い/読めないときのフォールバックの箱
  
	const IStageQuery* stage_ = nullptr;
	float verticalVelocity_ = 0.0f;
	bool grounded_ = false;
};
