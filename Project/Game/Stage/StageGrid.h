#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Vector3.h"
#include "Common/IStageQuery.h"

class Camera;
class PrimitiveInstance;

/// <summary>
/// CSV マップチップ 1 枚を読み込み、描画・当たり判定・破壊・リセットを引き受ける。
///
/// タスクリスト上は A の FlatFloorStage スタブに相当する暫定実装。
/// Day2 夕方に B の本物の StageGrid へ差し替える想定（IStageQuery は据え置き）。
///
/// CSV 仕様（マップチップ仕様書）:
///   - 32 列 × 18 行、整数のみ、UTF-8
///   - 一番上の行が画面の一番上（y は下方向に増える）。エンジンは Y-up なので読み込み時に上下反転して吸収する
///   - 0=空 / 1=プレイヤー初期位置 / 2=敵初期位置 / 10-19=壊れない床 / 20-29=壊れる床 / 30-39=ギミック
///   - 種別 = 値/10、テクスチャ = 値%10
///   - 1/2 のマスの地形は 0 扱い（読み込み時に落とす）
/// </summary>
class StageGrid : public IStageQuery {
public:
	static constexpr int   kCols = 32;
	static constexpr int   kRows = 18;
	static constexpr float kCellSize = 1.0f;
	static constexpr float kBreakableHP = 30.0f; // 壊れる床（20 番台）の初期 HP

	StageGrid();
	~StageGrid() override;

	/// <summary>
	/// CSV を読む。行数・列数が足りなければ 0 埋め＋Log 警告。
	/// ファイルを開けなければ最下段だけを床にしたフォールバックを組む（シーンは落とさない）。
	/// </summary>
	bool LoadFromCsv(const std::string& path);

	/// <summary>見た目（10=黒 / 20=白 の 1.0f 立方体）を生成する。LoadFromCsv の後に呼ぶ。</summary>
	void Initialize(Camera* camera);
	void Finalize();

	void Update();
	void Draw();

	/// <summary>
	/// 攻撃ヒットボックス（球）に重なる「壊れる床」へ damage を与え、
	/// HP が 0 以下になったセルを破壊する。破壊したセル数を返す。
	/// </summary>
	int DamageSphere(const Vector3& center, float radius, float damage);

	/// <summary>破壊した床を全て元に戻し、HP を初期値へ。ラウンド開始時に呼ぶ。</summary>
	void ResetTerrain();

	//==============================
	// スポーン
	//==============================
	bool HasPlayerSpawn() const { return hasPlayerSpawn_; }
	Vector3 GetPlayerSpawnWorld() const { return playerSpawnWorld_; }
	const std::vector<Vector3>& GetEnemySpawnsWorld() const { return enemySpawnsWorld_; }

	//==============================
	// セル問い合わせ
	//==============================
	bool InBounds(int cx, int cy) const { return cx >= 0 && cx < kCols && cy >= 0 && cy < kRows; }
	int  GetChip(int cx, int cy) const;         // 破壊済み / 範囲外は 0
	bool IsSolidCell(int cx, int cy) const;     // 10-29 かつ非破壊
	bool IsBreakableCell(int cx, int cy) const; // 20-29 かつ非破壊

	/// <summary>セル(cx,cy) の中心ワールド座標（Z=0）。</summary>
	Vector3 CellToWorldCenter(int cx, int cy) const;
	void    WorldToCell(const Vector3& p, int& cx, int& cy) const;

	//==============================
	// IStageQuery
	//==============================
	float GetCellSize() const override { return kCellSize; }
	bool  IsPointInsideBounds(const Vector3& p) const override;
	bool  OverlapsSolid(const Vector3& center, const Vector3& half) const override;
	StageMoveResult MoveAabb(const Vector3& from, const Vector3& to, const Vector3& half) const override;

private:
	struct Tile {
		int cx = 0;
		int cy = 0;
		int value = 0;        // 元の CSV 値（種別・テクスチャ選択に使う）
		bool destroyed = false;
		float hp = 0.0f;      // 壊れる床のみ意味を持つ
		std::unique_ptr<PrimitiveInstance> visual;
	};

	void ExtractSpawns();     // 1/2 を拾ってワールド座標に変換し、地形値を 0 に落とす

	Camera* camera_ = nullptr;

	int cells_[kRows][kCols] = {};         // 元データ（1/2 は 0 に落とし済み）
	int tileIndex_[kRows][kCols] = {};     // tiles_ への添字。-1 で「見た目なし」

	std::vector<Tile> tiles_;              // 見た目を持つセル（10/20 系）のみ

	bool hasPlayerSpawn_ = false;
	Vector3 playerSpawnWorld_{};
	std::vector<Vector3> enemySpawnsWorld_;
};
