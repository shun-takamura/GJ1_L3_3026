#pragma once

#include <memory>

#include "Vector3.h"
#include "Primitive/PrimitiveInstance.h"

class Camera;
class Weapon;
class IStageQuery;

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
///
/// 生成直後は必ずしも床の上とは限らない ── 投げ捨てた武器(ArcingProjectile)は
/// ブロックの側面や、真下に何も無い場所で着弾することがあるため、生成位置に
/// そのまま固定してしまうと「壁に引っかかったまま浮いている」ように見えるバグになる
/// (実際に報告された不具合)。そのため WeaponPickup 自身が簡易的な重力落下を持ち、
/// Character と同じ IStageQuery::MoveAabb で地形に着地するまで毎フレーム沈み続ける。
/// </summary>
class WeaponPickup {
public:
	WeaponPickup();
	~WeaponPickup();

	void Initialize(Camera* camera, const Vector3& position, std::unique_ptr<Weapon> weapon, const IStageQuery* stage);
	void Finalize();

	/// <summary>
	/// 毎フレーム呼ぶ。重力とIStageQuery::MoveAabbによる地形接地判定を毎フレーム行い続ける
	/// (着地後も判定自体は止めない)。これは、着地後に足場のブロックが破壊された場合でも
	/// 落下を再開できるようにするため(一度きりの判定だと、後から地形が壊れても検知できず
	/// 宙に浮いたまま残ってしまう不具合になる)。
	/// </summary>
	void Update(float dt);
	void Draw();

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
	std::unique_ptr<Weapon> weapon_;
	std::unique_ptr<PrimitiveInstance> visual_;
	const IStageQuery* stage_ = nullptr;
	float verticalVelocity_ = 0.0f;
	bool grounded_ = false; // 生成直後は必ず一度 MoveAabb で着地判定する(既に床の上でも1フレームだけ沈み込みを解決する)
};
