#pragma once

#include "Vector3.h"

/// <summary>
/// 「弾(または投げた武器)を1つ、この初期条件で飛ばしてほしい」という依頼をまとめた小包。
///
/// Weapon::TryRangedAttack (銃を撃った) や Character::ConsumePendingThrow (武器を投げた) が
/// これを生成し、GameScene が受け取って実際に ArcingProjectile を1つ生成する。
/// Character の AttackHitbox が「攻撃した/された」の橋渡しに使う小包なのと同じ役割を、
/// 「これから何かを飛ばす」側で担う。
/// </summary>
struct ProjectileSpawnRequest {
	Vector3 origin{ 0.0f, 0.0f, 0.0f }; // 発射位置(ワールド座標)
	float velocityX = 0.0f;            // 初速のX成分
	float velocityY = 0.0f;            // 初速のY成分
	float gravityScale = 1.0f;         // 重力の掛かり具合。1.0 で Character と同じ重力加速度になる
	float radius = 0.15f;              // 当たり判定球の半径
	float lifeTime = 3.0f;             // この秒数が経つと、何にも当たらなくても消える
	float damage = 0.0f;               // 命中時に与えるダメージ
	float knockbackPower = 0.0f;       // 命中時に与えるノックバックの大きさ(水平方向)

	// 0.0f(既定)なら爆風を持たない通常弾。0より大きい場合、着弾(キャラ命中/地形命中/
	// 寿命切れのいずれでも)した瞬間にその場で爆発し、爆心からこの半径以内にいる
	// 「発射者自身を含む」全キャラクターへ、距離に応じて減衰する damage/knockbackPower を
	// 別途与える(ArcingProjectile::GetBlastRadius, GameScene::ResolveExplosion 参照)。
	// 通常弾の直撃ダメージ(このstructのdamage/knockbackPower)とは別枠の効果。
	float blastRadius = 0.0f;

	// ---- 跳ね返り(リコシェット) ----
	// false(既定)なら今まで通り、地形に触れた瞬間に即死する(Pistol/AssaultRifle/Shotgun/Blaster/
	// 銃弾はすべてこのまま)。true にすると ArcingProjectile が地形接触で死なず、
	// IStageQuery::MoveAabb の hitWall/grounded を見て速度を反射させながら飛び続ける
	// (グレネードランチャー・投げ捨てた武器が使う。ArcingProjectile.h の設計コメント参照)。
	bool bounces = false;
	float wallRestitution = 0.0f;  // 壁反射時に残す速度の割合(0.6〜0.8想定)。bounces=falseなら無視される
	float floorRestitution = 0.0f; // 床反射時に残す速度の割合。0なら着地した瞬間に静止=死亡扱いになる
	                                // (投げ捨て武器はこちら)。0より大きいと床でも跳ね続ける(グレラン)。

	// 0(既定)なら反射回数無制限(グレネードランチャー・投げ捨てた武器はこのまま)。
	// 1以上なら、壁/床への反射がこの回数に達した後は次に地形へ触れても反射せず、
	// そのまま着弾して消える(リコシェットライフルの「反射は3回まで」用。
	// ArcingProjectile::bounceCount_ でカウントする)。
	int maxBounces = 0;

	// 0.0f(既定)なら無効。0より大きい場合、発射者以外のキャラクターがこの半径内に入った
	// 瞬間に(直撃していなくても)即座に死亡扱いにする近接センサー判定
	// (ArcingProjectile::TryProximityDetonate 参照。グレネードランチャー専用)。
	float proximityRadius = 0.0f;

	// ---- 距離ダメージ減衰 ----
	// 0.0f(既定)なら減衰なし。0より大きい場合、発射位置からの飛距離が0でdamage倍率1.0、
	// この値に達するとminDamageMultiplierまでdamageだけを線形減衰させる(knockbackPowerは
	// 対象外。ArcingProjectile::TryHitCharacter 参照。ショットガンの近距離特化調整用)。
	float damageFalloffRange = 0.0f;
	float minDamageMultiplier = 1.0f;

	// ---- 状態異常(氷銃・炎銃) ----
	// 既定値(倍率1.0/持続0)なら効果無し。直撃時に ArcingProjectile::TryHitCharacter が
	// Character::AttackHitbox へそのままコピーし、Character::ReceiveHit が適用する
	// (Character::AttackHitbox の同名フィールドと対。詳細はそちらのコメント参照)。
	float slowMultiplier = 1.0f;
	float slowDuration = 0.0f;
	float burnDps = 0.0f;
	float burnDuration = 0.0f;

	// ---- 着弾点を燃やす(炎銃専用) ----
	// false(既定)なら何もしない。true の場合、着弾(キャラ命中/地形命中/寿命切れいずれでも)した
	// 位置に、半径 fireHazardRadius・持続 fireHazardDuration 秒の炎(GameScene::FireHazard)を
	// 1つ残す。踏んでいるキャラは発射者自身を含め fireHazardDps の継続ダメージを受け続ける
	// (Blaster の「自分の爆風にも巻き込まれる」と同じ、位置関係そのものをリスクにする設計)。
	// 直撃時の burnDps/burnDuration(このキャラに撃ち込まれた瞬間の効果)とは別枠。
	bool spawnsFireHazard = false;
	float fireHazardRadius = 0.0f;
	float fireHazardDuration = 0.0f;
	float fireHazardDps = 0.0f;
};
