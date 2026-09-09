#pragma once

/// <summary>チュートリアルの説明 1 段ぶん。</summary>
struct TutorialStep {
	// 画面下パネルの見出し（1行目）。
	const char* title = "";
	// 本文。キーボード＆マウス操作時に出す方。
	const char* bodyKeyboard = "";
	// 本文。コントローラー接続時に出す方（キー表記だけを差し替えた同じ内容）。
	const char* bodyPad = "";
	// この段に入った瞬間、プレイヤーの近くに拾える武器を 1 つ置く。
	bool spawnWeapon = false;
	// 最終段。SPACE では進まず、「敵を倒す」ことでチュートリアルが完了する。
	bool waitForKill = false;
};

/// <summary>
/// チュートリアルの説明進行だけを持つ小さいクラス。
///
/// 「今どの説明を出しているか」と「次へ進んだか」しか知らない ── ステージ・キャラ・
/// 描画には一切触れない（GameScene が CurrentStep() を読んで画面下に描き、
/// 完了条件の判定も GameScene が行う）。ステップの追加・文言の修正は
/// TutorialDirector.cpp のテーブル 1 か所だけで済む。
/// </summary>
class TutorialDirector {
public:
	/// <summary>最初の段へ戻す（チュートリアル開始時に呼ぶ）。</summary>
	void Reset();

	/// <summary>
	/// advancePressed（SPACE / パッドの (B) が押された瞬間）なら次の段へ進める。
	/// 最終段（waitForKill）では何もしない ── そこから先は敵を倒すまで進まないため。
	/// </summary>
	void Update(bool advancePressed);

	const TutorialStep& CurrentStep() const;

	/// <summary>今が何段目か（0 始まり）と全体の段数。画面の「3 / 12」表示用。</summary>
	int StepIndex() const { return index_; }
	int StepCount() const;

	/// <summary>全ての説明が終わり、「敵を倒せ」の最終段に居るか。</summary>
	bool IsAwaitingFinalKill() const { return CurrentStep().waitForKill; }

	/// <summary>武器を 1 つ置く要求が立っていれば true を返して下ろす（1回きり）。</summary>
	bool ConsumeWeaponSpawnRequest();

	/// <summary>武器を置き直させる（死亡リセットで素手に戻ったとき用）。</summary>
	void RequestWeaponSpawn() { weaponSpawnPending_ = true; }

	/// <summary>武器の説明段以降まで進んでいるか（＝ステージに武器があるべきか）。</summary>
	bool HasReachedWeaponStep() const;

private:
	int index_ = 0;
	bool weaponSpawnPending_ = false;
};
