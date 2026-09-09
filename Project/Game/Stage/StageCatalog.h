#pragma once

#include <string>
#include <vector>

/// <summary>
/// Resources/Stages/ 直下の *.csv を 1 ステージ = 1 ファイルとして列挙するだけの軽いカタログ。
///
/// GameScene が起動時に構築し、ランダムに 1 枚選んで読み込む。デバッグ ImGui からの
/// ステージ切り替えでも、この一覧の index を指定して使う。
/// 本番のラウンド進行（次ステージ選出）でもそのまま流用できる形にしてある。
/// </summary>
class StageCatalog {
public:
	/// <summary>Resources/Stages/ を走査して *.csv を名前順に集める。
	/// フォルダが無い / 空の場合は Sample_00 だけを持つフォールバックになる。</summary>
	void Scan();

	int Count() const { return static_cast<int>(entries_.size()); }
	bool Empty() const { return entries_.empty(); }

	/// <summary>表示名（拡張子なしのファイル名）。範囲外は空文字。</summary>
	const std::string& NameAt(int index) const;

	/// <summary>エンジンに渡す相対パス（"Resources/Stages/xxx.csv"）。範囲外は空文字。</summary>
	const std::string& PathAt(int index) const;

	/// <summary>RandomGenerator 経由で 1 枚選ぶ（シード再現性を壊さない）。空なら 0。</summary>
	int PickRandomIndex() const;

	/// <summary>
	/// 本編用のステージ抽選。名前に "Sample" を含むステージ（タイトル/チュートリアル用）は
	/// 除外し、シャッフルバッグ方式で「全ステージを一巡するまで同じステージを返さない」。
	/// バッグを使い切ったら自動で再シャッフルする（一巡境界での連続だけは避ける）。
	/// 除外後に 1 枚も残らなければ従来の抽選へフォールバックしてゲームを止めない。
	/// </summary>
	int PickNextBattleIndex();

	/// <summary>シャッフルバッグを空にし、次の PickNextBattleIndex から新しい一巡を始める。
	/// セット開始時（どちらかが 10 点を取って 1 セット決着した後）に呼ぶ。</summary>
	void ResetBattleRotation();

private:
	struct Entry {
		std::string name;
		std::string path;
	};
	std::vector<Entry> entries_;

	// 本編抽選の対象（"Sample" を除いた entries_ の index）。Scan() で作る。
	std::vector<int> battlePool_;
	// 現在の一巡でまだ出していない entries_ の index。空なら次に引く前へ再充填する。
	std::vector<int> battleBag_;
	// 直前に PickNextBattleIndex が返した entries_ の index（一巡境界での連続回避用）。-1 で無し。
	int lastBattlePick_ = -1;
};
