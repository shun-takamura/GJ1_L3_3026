#pragma once

#include "Weapon.h"

/// <summary>
/// 散弾銃。1回のトリガーで複数弾(ペレット)を扇状に同時発射する。
/// 短射程(寿命が短く、すぐ消える)・高威力・大きめの反動を持つ。
/// </summary>
class Shotgun : public Weapon {
public:
	bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) override;

	float GetRecoilPower() const override { return kRecoilPower; }
	std::string GetName() const override { return "Shotgun"; }
	int GetRemainingAmmo() const override { return ammo_; }
	std::string GetModelDirectory() const override { return "Resources/Models/Shotgun"; }
	std::string GetModelFileName() const override { return "Shotgun.mesh"; }

	/// <summary>「Weapon Tuning」ImGuiウィンドウから呼ばれるバランス調整スライダー群(Pistol.h 参照)。</summary>
	static void DrawImGuiTuning();

private:
	// static inline(constexprではない)。ImGuiで実行時に調整できるようにするため(Pistol.h の設計コメント参照)。
	static inline int   kStartingAmmo = 6;
	static inline int   kPelletCount = 6;         // 1トリガーで飛び出すペレット数
	// 散弾の広がり角(照準方向を中心に±この角度)。以前は0.35だったが、kRadius(ペレット1発の
	// 判定半径)が小さいのと相まって、点ではなく面積を持つキャラの当たり判定カプセル(半径0.45)
	// に対して至近距離ですら6発中1〜2発しか当たらない計算になっていた(角度spread×距離の
	// 横方向のズレが、カプセル半径+ペレット半径の合計をすぐ超えてしまうため)。0.22まで狭め、
	// 下の kRadius も広げることで「至近〜中距離では大半のペレットが実際に当たる」ようにしてある。
	static inline float kSpreadAngleRad = 0.22f;
	static inline float kCooldown = 0.8f;         // 単発威力が高い分、次弾までの間隔も長い
	static inline float kMuzzleSpeed = 16.0f;
	static inline float kDamagePerPellet = 9.0f;  // 至近距離で全弾命中すると素手より遥かに高威力
	static inline float kKnockbackPower = 8.0f;
	// 反動公式(移動距離 ≈ power/6.0)で約2.33ユニット。以前は9.0(≈1.5ユニット)で「近距離特化の
	// 割に反動が控えめ」という違和感があったため引き上げた。
	static inline float kRecoilPower = 14.0f;
	// ---- 距離ダメージ減衰 ----
	// 以前は距離に関わらずダメージが一定で、「近距離特化」を謳う割に遠くから撃っても
	// 弱くならないという違和感があった。飛距離が kDamageFalloffRange に達するまでに
	// ペレット1発ぶんのダメージを kMinDamageMultiplier まで線形に落とす
	// (kMuzzleSpeed×kLifeTime≈9.6が理論上の最大飛距離なので、その手前で減衰し切るようにしてある)。
	static inline float kDamageFalloffRange = 6.0f;
	static inline float kMinDamageMultiplier = 0.35f; // 5.0 × 0.35 ≈ 1.75(遠距離での1発ぶん)
	static inline float kGravityScale = 1.2f;
	// ペレット1発の当たり判定半径。以前は0.12(見た目通りの小さい弾)だったが、それだと
	// kSpreadAngleRad をどれだけ絞っても複数ペレットを同時に当てるのが事実上不可能だった
	// (弾同士の間隔がすぐ判定半径を上回るため)。「見た目は小さい弾のまま、当たり判定だけ
	// 少し寛容にする」典型的な処理で 0.3 まで広げている。
	static inline float kRadius = 0.3f;
	static inline float kLifeTime = 0.6f;         // 短射程(すぐ寿命切れで消える)
	static constexpr float kMuzzleForwardOffset = 1.0f;

	int ammo_ = kStartingAmmo;
	float cooldownTimer_ = 0.0f;
};
