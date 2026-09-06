#include "Stage/StageCatalog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "Log.h"
#include "RandomGenerator.h"

namespace {
	const std::string kEmpty;
	// エンジンはカレントディレクトリ（= Project/）からの相対で Resources/ を読む。
	constexpr const char* kStagesDir = "Resources/Stages";
}

void StageCatalog::Scan() {
	entries_.clear();

	std::error_code ec;
	const std::filesystem::path dir(kStagesDir);
	if (std::filesystem::is_directory(dir, ec)) {
		for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
			if (ec) break;
			if (!de.is_regular_file()) continue;
			const std::filesystem::path& p = de.path();
			std::string ext = p.extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (ext != ".csv") continue;

			Entry e;
			e.name = p.stem().string();
			// パス区切りは '/' に正規化（エンジンの ifstream はどちらでも開けるが表示の一貫性のため）。
			e.path = std::string(kStagesDir) + "/" + p.filename().string();
			entries_.push_back(std::move(e));
		}
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
