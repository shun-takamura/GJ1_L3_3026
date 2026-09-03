#pragma once

/// <summary>
/// 「このフレーム、キャラに何をさせたいか」を表す解決済みの意図。
///
/// プレイヤー操作(GameScene が InputManager から読む)も、AI の行動決定
/// (EnemyBrain)も、最終的にこの構造体を1つ埋めて Character へ渡す、という
/// 1本の経路に統一するためのもの。Character 自身はこの型を知らなくてよい
/// (GameScene が各フィールドを Character::Update() の引数へ展開する)。
///
/// ※ これは計画上の凍結契約 GameTypes.h(CharacterInput/タグ/DamageInfo)の
///   最小の種。武器・AI・ステージ担当とすり合わせたうえで Day1 に正式版へ
///   差し替え、B と凍結し直す前提。現状は Character::Update() の引数と
///   1:1 対応しているだけ。
/// </summary>
struct CharacterInput {
	float moveX = 0.0f;            // 左右の移動量。-1.0〜1.0
	bool jumpTriggered = false;    // ジャンプ入力が押された「瞬間」か
	bool crouchHeld = false;       // しゃがみ入力が押されている「間」ずっと true
	float aimDirX = 1.0f;          // 照準方向(正規化前でよい。Character 側で正規化される)
	float aimDirY = 0.0f;
	bool attackTriggered = false;  // 攻撃入力が押された「瞬間」か
	bool attackHeld = false;       // 攻撃入力が押されている「間」か(連射武器用)
	bool throwTriggered = false;   // 武器投げ捨て入力が押された「瞬間」か
};
