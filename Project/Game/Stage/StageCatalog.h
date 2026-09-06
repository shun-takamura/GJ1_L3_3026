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

private:
	struct Entry {
		std::string name;
		std::string path;
	};
	std::vector<Entry> entries_;
};
