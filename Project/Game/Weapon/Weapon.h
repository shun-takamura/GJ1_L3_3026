#pragma once

#include <string>
#include <vector>

#include "Vector3.h"
#include "Character/Character.h"
#include "ProjectileSpawnRequest.h"

/// <summary>
/// Character が装備する武器の共通インターフェース。
///
/// Character は「今どの Weapon を1つ持っているか」だけを知っていて、反動・弾道・
/// 連射方式・残弾など武器ごとの違いは一切知らない(Character.h の設計コメントと対)。
/// 素手(UnarmedWeapon)も「投げ捨てられない Weapon の一種」として同じ枠組みで扱うことで、
/// Character 側に「武器を持っていない」という特別な状態(nullptr 分岐)を作らずに済む。
///
/// 近接武器(素手)と飛び道具(銃)で「攻撃が成立したときに何を返すか」がそもそも違う
/// (即座に当たり判定を取るヒットボックス vs 後から飛んでいく弾)ため、
/// TryMeleeAttack / TryRangedAttack の2系統に分けている。武器ごとにどちらか片方だけを
/// オーバーライドすればよい(もう片方はデフォルト実装が何もせず false を返す)。
/// </summary>
class Weapon {
public:
	virtual ~Weapon() = default;

	/// <summary>
	/// 近接攻撃を試みる。近接武器(今のところ素手のみ)だけがオーバーライドする。
	/// 攻撃が成立したら true を返し、outHitbox にヒットボックス情報を詰める。
	/// </summary>
	/// <param name="ownerPos">攻撃する側(装備者)の現在位置</param>
	/// <param name="aimDirX">照準方向のX成分(正規化済み)</param>
	/// <param name="aimDirY">照準方向のY成分(正規化済み)</param>
	virtual bool TryMeleeAttack(float dt, bool triggered, const Vector3& ownerPos,
		float aimDirX, float aimDirY, Character::AttackHitbox& outHitbox) {
		(void)dt; (void)triggered; (void)ownerPos; (void)aimDirX; (void)aimDirY; (void)outHitbox;
		return false;
	}

	/// <summary>
	/// 飛び道具の発射を試みる。銃系の武器だけがオーバーライドする。
	/// 発射が成立したら true を返し、outSpawns に生成してほしい弾のリクエストを積む
	/// (ショットガンのように1トリガーで複数弾を同時に出す武器があるため vector)。
	/// </summary>
	/// <param name="triggered">攻撃入力が押された「瞬間」か(単発武器用)</param>
	/// <param name="held">攻撃入力が押されている「間」か(連射武器用)</param>
	virtual bool TryRangedAttack(float dt, bool triggered, bool held, const Vector3& ownerPos,
		float aimDirX, float aimDirY, std::vector<ProjectileSpawnRequest>& outSpawns) {
		(void)dt; (void)triggered; (void)held; (void)ownerPos; (void)aimDirX; (void)aimDirY; (void)outSpawns;
		return false;
	}

	/// <summary>発射が成立したときに持ち主自身へ与える反動(自分へのノックバックの大きさ)。既定値は反動なし。</summary>
	virtual float GetRecoilPower() const { return 0.0f; }

	/// <summary>投げ捨てられる武器か。素手(UnarmedWeapon)だけ false を返す。</summary>
	virtual bool CanBeThrown() const { return true; }

	/// <summary>HUD表示用の武器名。</summary>
	virtual std::string GetName() const = 0;

	/// <summary>HUD表示用の残弾数。弾の概念が無い武器(素手など)は kInfiniteAmmo を返す。</summary>
	virtual int GetRemainingAmmo() const { return kInfiniteAmmo; }

	static constexpr int kInfiniteAmmo = -1;
};
