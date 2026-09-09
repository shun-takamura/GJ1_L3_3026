#pragma once

/// <summary>
/// ごく軽いセーブデータ。今のところ「チュートリアルを完了したか」1項目だけを持つ。
///
/// 保存先は実行時のカレントディレクトリ直下の SaveData.txt。
///   - 開発中(VS からの実行) : Project/SaveData.txt
///     （.vcxproj の LocalDebuggerWorkingDirectory が $(SolutionDir) = Project/ のため）
///   - 配布版               : exe と同じフォルダ（package_release.py が作る zip はフラット構成）
///
/// 形式は 1行1項目の "key=value"（UTF-8/ASCII のみ）。ファイルが無い・壊れている場合は
/// 「何も達成していない初期状態」として扱い、絶対に例外を投げない・ゲームを止めない。
/// ＝ セーブファイルの同梱漏れや手動削除でも、単に「チュートリアルから始まる」だけで済む。
///
/// 追加する項目は Load()/Save() の両方に書き足すこと（項目数が増えるなら JSON へ移行する）。
/// 保存フォーマットを変えた場合は tools/Python/package_release.py の DEFAULT_SAVE_CONTENT も
/// 合わせて更新する（配布 zip に同梱する初期状態のセーブデータ）。
/// </summary>
namespace SaveData {

	/// <summary>
	/// セーブファイルを読み込む。以降の Get 系はメモリ上の値を返す。
	/// 明示的に呼ばなくても、最初の参照時に自動で1回だけ読み込まれる。
	/// </summary>
	void Load();

	/// <summary>チュートリアルを完了済みか（ファイルが無ければ false）。</summary>
	bool IsTutorialCleared();

	/// <summary>チュートリアル完了フラグを設定し、その場でファイルへ書き出す。</summary>
	void SetTutorialCleared(bool cleared);

	/// <summary>全項目を初期状態へ戻してファイルへ書き出す（デバッグ用）。</summary>
	void ResetAll();

	/// <summary>セーブファイルのパス（ログ・ImGui 表示用）。</summary>
	const char* GetFilePath();

}
