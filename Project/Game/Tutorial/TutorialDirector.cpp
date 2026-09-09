#include "Tutorial/TutorialDirector.h"

namespace {
	// 説明の並び。上から順に SPACE（パッドは (B)）で送っていく。
	// bodyKeyboard / bodyPad は「同じ内容でキー表記だけ違う」ペアにしてある
	// （GameScene がコントローラーの接続状態を見てどちらを描くか決める）。
	//
	// 操作の対応は GameScene::Update() の入力読み取りと 1:1 で揃えること:
	//   移動 A/D or 左スティック / ジャンプ W or (A) / しゃがみ S or 左スティック下・十字下 /
	//   攻撃 左クリック or (X) / 投げ捨て R・右クリック or (Y)
	const TutorialStep kSteps[] = {
		{
			"移動とジャンプ",
			"A / D キーで左右に移動、W キーでジャンプ。壁に張り付いている間は W で壁キックできる。",
			"左スティックで左右に移動、(A) ボタンでジャンプ。壁に張り付いている間は (A) で壁キックできる。",
		},
		{
			"しゃがむ",
			"S キーでしゃがむ。当たり判定が低くなるので、頭上をかすめる弾を避けられる。しゃがんだまま歩けば低い隙間もくぐれる。",
			"左スティックを下、または十字キー下でしゃがむ。当たり判定が低くなるので、頭上をかすめる弾を避けられる。しゃがんだまま歩けば低い隙間もくぐれる。",
		},
		{
			"殴る",
			"左クリックで攻撃。武器を持っていなければ素手で殴る。素手でも木のブロックは壊せる。",
			"(X) ボタンで攻撃。武器を持っていなければ素手で殴る。素手でも木のブロックは壊せる。",
		},
		{
			"武器を拾って撃つ",
			"落ちている武器に重なると拾える。左クリックで発射、R キーか右クリックで投げ捨てる。武器は試合中もステージに湧き続ける。",
			"落ちている武器に重なると拾える。(X) ボタンで発射、(Y) ボタンで投げ捨てる。武器は試合中もステージに湧き続ける。",
			true, // この段で拾える武器を1つ置く
		},
		{
			"ギミック：ベルトコンベア",
			"矢印の向きに流れる床。乗っている間ずっと横へ運ばれる。逆向きに歩けば耐えられるが、端まで運ばれると落ちる。",
			"矢印の向きに流れる床。乗っている間ずっと横へ運ばれる。逆向きに歩けば耐えられるが、端まで運ばれると落ちる。",
		},
		{
			"ギミック：トゲ",
			"触れた瞬間に即死する。上を通るときはジャンプで飛び越すこと。相手をトゲへ吹き飛ばすのも有効。",
			"触れた瞬間に即死する。上を通るときはジャンプで飛び越すこと。相手をトゲへ吹き飛ばすのも有効。",
		},
		{
			"ギミック：爆弾ブロック",
			"赤いブロックは攻撃を当てると点滅を始め、少し経って爆発する。近くのブロックへ誘爆し、巻き込まれると大ダメージ＋吹き飛ばされる。",
			"赤いブロックは攻撃を当てると点滅を始め、少し経って爆発する。近くのブロックへ誘爆し、巻き込まれると大ダメージ＋吹き飛ばされる。",
		},
		{
			"ギミック：ポータル",
			"渦に入るともう一方の渦へワープする。追い詰められたときの逃げ道にも、回り込みにも使える。",
			"渦に入るともう一方の渦へワープする。追い詰められたときの逃げ道にも、回り込みにも使える。",
		},
		{
			"ギミック：壊れる床",
			"木のブロックは攻撃で壊せる。相手の足場を撃ち抜いて場外へ落とすのが基本の勝ち筋。自分の足場を壊さないように。",
			"木のブロックは攻撃で壊せる。相手の足場を撃ち抜いて場外へ落とすのが基本の勝ち筋。自分の足場を壊さないように。",
		},
		{
			"ルール：10 ポイント先取",
			"先に 10 ポイント取った方が勝ち。1 ポイント入るたびにステージがランダムで切り替わる。",
			"先に 10 ポイント取った方が勝ち。1 ポイント入るたびにステージがランダムで切り替わる。",
		},
		{
			"ルール：やられると相手に 1 ポイント",
			"HP が 0 になる、または場外へ落ちると相手に 1 ポイント入る。トゲや自分の爆風での自滅も同じ扱い。",
			"HP が 0 になる、または場外へ落ちると相手に 1 ポイント入る。トゲや自分の爆風での自滅も同じ扱い。",
		},
		{
			"最後に：敵を倒せ",
			"説明は以上。目の前の敵を倒すとチュートリアル終了、本番のステージへ進む。",
			"説明は以上。目の前の敵を倒すとチュートリアル終了、本番のステージへ進む。",
			false,
			true, // ここは SPACE では進まない。敵を倒したら完了
		},
	};
	constexpr int kStepCount = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));

	/// <summary>spawnWeapon が立っている最初の段（＝武器の説明段）の index。無ければ kStepCount。</summary>
	int WeaponStepIndex() {
		for (int i = 0; i < kStepCount; ++i) {
			if (kSteps[i].spawnWeapon) {
				return i;
			}
		}
		return kStepCount;
	}
}

void TutorialDirector::Reset() {
	index_ = 0;
	weaponSpawnPending_ = kSteps[0].spawnWeapon;
}

void TutorialDirector::Update(bool advancePressed) {
	if (!advancePressed) {
		return;
	}
	// 最終段（敵を倒すのを待つ段）から先へは進まない。
	if (kSteps[index_].waitForKill || index_ >= kStepCount - 1) {
		return;
	}
	++index_;
	if (kSteps[index_].spawnWeapon) {
		weaponSpawnPending_ = true;
	}
}

const TutorialStep& TutorialDirector::CurrentStep() const {
	const int i = (index_ < 0) ? 0 : ((index_ >= kStepCount) ? kStepCount - 1 : index_);
	return kSteps[i];
}

int TutorialDirector::StepCount() const {
	return kStepCount;
}

bool TutorialDirector::ConsumeWeaponSpawnRequest() {
	const bool pending = weaponSpawnPending_;
	weaponSpawnPending_ = false;
	return pending;
}

bool TutorialDirector::HasReachedWeaponStep() const {
	return index_ >= WeaponStepIndex();
}
