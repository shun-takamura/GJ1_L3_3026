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
};
