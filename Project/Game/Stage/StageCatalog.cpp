#include "Stage/StageCatalog.h"

#include <algorithm>

#include "AssetLocator.h"
#include "Log.h"
#include "RandomGenerator.h"

namespace {
	const std::string kEmpty;
	// エンジンはカレントディレクトリ（= Project/）からの相対で Resources/ を読む。
	constexpr const char* kStagesDir = "Resources/Stages";

	// "Resources/Stages/Starge_01.csv" -> "Starge_01"
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
