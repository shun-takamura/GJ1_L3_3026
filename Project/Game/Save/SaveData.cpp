#include "Save/SaveData.h"

#include <fstream>
#include <string>

#include "Log.h"

namespace {
	// カレントディレクトリ直下。AssetLocator は使わない ── あれは読み取り専用のアセット
	// （FS or Assets.pack）を引くためのものなので、書き込みが要るセーブデータには使えない。
	constexpr const char* kSaveFilePath = "SaveData.txt";

	constexpr const char* kKeyTutorialCleared = "tutorialCleared";

	bool s_loaded = false;
	bool s_tutorialCleared = false;

	/// <summary>現在のメモリ上の値を丸ごと書き出す。失敗してもゲームは止めない（ログのみ）。</summary>
	void Save() {
		std::ofstream ofs(kSaveFilePath, std::ios::binary | std::ios::trunc);
		if (!ofs) {
			Log(std::string("SaveData: 書き込みに失敗しました -> ") + kSaveFilePath + "\n");
			return;
		}
		ofs << kKeyTutorialCleared << "=" << (s_tutorialCleared ? 1 : 0) << "\n";
	}

	/// <summary>最初の参照時に1回だけ読み込む。</summary>
	void EnsureLoaded() {
		if (!s_loaded) {
			SaveData::Load();
		}
	}
}

void SaveData::Load() {
	s_loaded = true;
	// 読めなかった項目は初期値のまま（＝何も達成していない状態）。
	s_tutorialCleared = false;

	std::ifstream ifs(kSaveFilePath, std::ios::binary);
	if (!ifs) {
		Log(std::string("SaveData: セーブデータが無いので初期状態で開始 -> ") + kSaveFilePath + "\n");
		return;
	}

	std::string line;
	while (std::getline(ifs, line)) {
		// 改行コードが CRLF のファイルでも値の末尾に \r を残さない。
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
			line.pop_back();
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos) {
			continue; // 空行・コメント・壊れた行は黙って無視する
		}
		const std::string key = line.substr(0, eq);
		const std::string value = line.substr(eq + 1);
		if (key == kKeyTutorialCleared) {
			s_tutorialCleared = (value == "1" || value == "true");
		}
	}
	Log(std::string("SaveData: 読み込み完了 tutorialCleared=")
		+ (s_tutorialCleared ? "1" : "0") + "\n");
}

bool SaveData::IsTutorialCleared() {
	EnsureLoaded();
	return s_tutorialCleared;
}

void SaveData::SetTutorialCleared(bool cleared) {
	EnsureLoaded();
	s_tutorialCleared = cleared;
	Save();
	Log(std::string("SaveData: tutorialCleared=") + (cleared ? "1" : "0") + " を保存\n");
}

void SaveData::ResetAll() {
	s_loaded = true;
	s_tutorialCleared = false;
	Save();
	Log("SaveData: セーブデータを初期状態へ戻した\n");
}

const char* SaveData::GetFilePath() {
	return kSaveFilePath;
}
