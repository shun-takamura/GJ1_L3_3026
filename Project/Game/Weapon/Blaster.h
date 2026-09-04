#pragma once

#include "Weapon.h"

/// <summary>
/// 至近距離爆風砲。着弾(キャラ命中/地形命中/寿命切れいずれでも)した瞬間にその場で
/// 爆発し、爆心に近いほど強いダメージ/ノックバックを発射者自身を含む全キャラクターへ
/// 与える(距離減衰は ProjectileSpawnRequest::blastRadius /
/// GameScene::ResolveExplosion 側の仕事。 このクラスは kDamage/kKnockbackPower
/// を「爆心での最大値」として渡すだけでよい)。
///
/// 「与える影響がデカいほど自分へのリスクもデカい」を数値の後付けではなく位置関係そのもの
/// で成立させるための武器 ──
/// 発射時の反動(kRecoilPower)に加えて、至近距離で撃てば
/// 自分の爆風にも巻き込まれる。持ち込み弾数は3種の銃の中で最も少ない代わりに、
/// 1発の破壊力(直撃時のダメージ・ノックバックとも最大)も最も大きい。
/// </summary>
class Blaster : public Weapon {
public:
  bool TryRangedAttack(float dt, bool triggered, bool held,
                       const Vector3 &ownerPos, float aimDirX, float aimDirY,
                       std::vector<ProjectileSpawnRequest> &outSpawns) override;

  float GetRecoilPower() const override { return kRecoilPower; }
  std::string GetName() const override { return "Blaster"; }
  int GetRemainingAmmo() const override { return ammo_; }
  std::string GetModelDirectory() const override {
    return "Resources/Models/Blaster";
  }
  std::string GetModelFileName() const override { return "Blaster.mesh"; }

  /// <summary>「Weapon
  /// Tuning」ImGuiウィンドウから呼ばれるバランス調整スライダー群(Pistol.h
  /// 参照)。</summary>
  static void DrawImGuiTuning();

private:
  // static
  // inline(constexprではない)。ImGuiで実行時に調整できるようにするため(Pistol.h
  // の設計コメント参照)。
  static inline int kStartingAmmo = 3;  // 銃系の中で最少(1発の重さで補う)
  static inline float kCooldown = 1.2f; // 単発武器の中でも最長のクールダウン
  static inline float kMuzzleSpeed =
      10.0f; // 3種の銃の中で最も遅い(山なりが最も目立つ、避けやすい)
  static inline float kDamage =
      26.0f; // 爆心(距離0)での最大ダメージ。減衰込みなので直撃なら他武器より高威力
  static inline float kKnockbackPower =
      16.0f; // 爆心での最大ノックバック。至近距離なら場外まで届く
  static inline float kRecoilPower = 16.0f; // 発射時の反動。他武器より大きめ
  // 弾速は据え置きで遅いままにしつつ、重力を弱め(1.3→0.9)・寿命を延ばして(2.5→4.0)
  // 「遅いが遠くまで届く」山なり弾にしてある(弾速だけでは飛距離が伸びないため)。
  static inline float kGravityScale = 0.9f;
  static inline float kRadius = 0.2f;
  static inline float kLifeTime = 4.0f;
  static constexpr float kMuzzleForwardOffset = 1.0f;
  static inline float kBlastRadius =
      2.5f; // 爆風の届く範囲(ステージ1セル=1.0の2.5倍ぶん)

  int ammo_ = kStartingAmmo;
  float cooldownTimer_ = 0.0f;
};
