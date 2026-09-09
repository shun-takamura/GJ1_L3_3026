#include "Stage/StageCatalog.h"

#include <algorithm>

#include "AssetLocator.h"
#include "Log.h"
#include "RandomGenerator.h"

namespace {
	const std::string kEmpty;
	// エンジンはカレントディレクトリ（= Project/）からの相対で Resources/ を読む。
	constexpr const char* kStagesDir = "Resources/Stages";

	// "Resources/Stages/Stage_01.csv" -> "Stage_01"
	std::string StemOf(const std::string& path) {
		size_t slash = path.find_last_of("/\\");
		std::string file = (slash == std::string::npos) ? path : path.substr(slash + 1);
		size_t dot = file.find_last_of('.');
		return (dot == std::string::npos) ? file : file.substr(0, dot);
	}
}

void StageCatalog::Scan() {
	entries_.clear();

	// FS モードでもディスクを走査、pack モードでは pack 目次から拾う（AssetLocator が両対応）。
	for (const std::string& p : AssetLocator::GetInstance()->ListByExtension(".csv", kStagesDir)) {
		Entry e;
		e.name = StemOf(p);
		e.path = p;
		entries_.push_back(std::move(e));
	}

	std::sort(entries_.begin(), entries_.end(),
		[](const Entry& a, const Entry& b) { return a.name < b.name; });

	if (entries_.empty()) {
		Log("StageCatalog: Resources/Stages/ に CSV が見つかりません -> Sample_00 のみで起動\n");
		entries_.push_back({ "Sample_00", "Resources/Stages/Sample_00.csv" });
	}

	// 本編抽選プール。名前に "Sample"（タイトルのデモ用）または "Tutorial"（チュートリアル専用）
	// を含むものは、本編のラウンドでは絶対に出さないので除外する。
	battlePool_.clear();
	for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
		const std::string& n = entries_[i].name;
		if (n.find("Sample") == std::string::npos && n.find("Tutorial") == std::string::npos) {
			battlePool_.push_back(i);
		}
	}
	ResetBattleRotation();
}

const std::string& StageCatalog::NameAt(int index) const {
	if (index < 0 || index >= static_cast<int>(entries_.size())) return kEmpty;
	return entries_[index].name;
}

const std::string& StageCatalog::PathAt(int index) const {
	if (index < 0 || index >= static_cast<int>(entries_.size())) return kEmpty;
	return entries_[index].path;
}

int StageCatalog::PickRandomIndex() const {
	if (entries_.empty()) return 0;
	return RandomGenerator::Instance().NextInt(0, static_cast<int>(entries_.size()) - 1);
}

void StageCatalog::ResetBattleRotation() {
	battleBag_.clear();
	lastBattlePick_ = -1;
}

int StageCatalog::PickNextBattleIndex() {
	if (battlePool_.empty()) {
		// "Sample" 以外が 1 枚も無い（＝実質 Sample しか無い）。ゲームを止めないため従来抽選へ。
		return PickRandomIndex();
	}

	if (battleBag_.empty()) {
		battleBag_ = battlePool_;  // 新しい一巡ぶんを充填する。
	}

	// バッグからランダムに 1 枚引いて取り除く（＝一巡の間は重複しない）。
	// 再シャッフル直後に直前と同じステージを引いてしまい、かつ他に候補があるなら引き直す。
	int slot = 0;
	int pick = -1;
	for (int attempt = 0; attempt < 4; ++attempt) {
		slot = RandomGenerator::Instance().NextInt(0, static_cast<int>(battleBag_.size()) - 1);
		pick = battleBag_[slot];
		if (pick != lastBattlePick_ || battleBag_.size() == 1) {
			break;
		}
	}
	battleBag_.erase(battleBag_.begin() + slot);
	lastBattlePick_ = pick;
	return pick;
}
