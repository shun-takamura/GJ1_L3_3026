#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Vector3.h"
#include "Common/IStageQuery.h"

class Camera;
class PrimitiveInstance;
class Object3DInstance;
class Object3DManager;
class DirectXCore;

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

	// ---- ギミック（30 番台）----
	// CSV 値 %10 で種類を選ぶ: 30=左ベルト / 31=右ベルト / 32=トゲ / 33=爆弾ブロック / 34=ポータル
	enum class GimmickType { None, BeltLeft, BeltRight, Spike, Bomb, Portal };

	static constexpr float kBombFuseSeconds = 3.0f;       // 被弾から起爆まで
	static constexpr float kBombChainFuseSeconds = 0.12f; // 誘爆時の遅延（連鎖の見た目用）
	static constexpr float kBombRadiusCells = 2.5f;       // 爆風半径（ブロック単位）
	static constexpr float kBombDamage = 50.0f;

	/// <summary>爆弾ブロックが起爆したときに 1 件ずつ積まれる。GameScene が ConsumeBombExplosions で回収し、
	/// キャラへのダメージ／吹っ飛ばしを適用する（地形削り・誘爆は StageGrid 内で完結済み）。</summary>
	struct BombExplosion {
		Vector3 center{};
		float radius = 0.0f;
		float damage = 0.0f;
	};

	StageGrid();
	~StageGrid() override;

	/// <summary>
	/// CSV を読む。行数・列数が足りなければ 0 埋め＋Log 警告。
	/// ファイルを開けなければ最下段だけを床にしたフォールバックを組む（シーンは落とさない）。
	/// </summary>
	bool LoadFromCsv(const std::string& path);

	/// <summary>見た目（10=黒 / 20=白 の 1.0f 立方体、ギミックの仮ボックス、トゲモデル）を生成する。
	/// LoadFromCsv の後に呼ぶ。o3d/dxCore はトゲ（Spike.mesh）用。nullptr ならトゲは非表示になるだけ。</summary>
	void Initialize(Camera* camera, Object3DManager* object3DManager = nullptr, DirectXCore* dxCore = nullptr);
	void Finalize();

	void Update(float dt);
	void Draw();

	/// <summary>ギミックのうち Object3D モデル（トゲ）を描画する。
	/// GameScene が Object3DManager::DrawSetting / BindLights の後にまとめて呼ぶ。</summary>
	void DrawModels(DirectXCore* dxCore);

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
	// ギミック（30 番台）
	// GameScene が毎フレーム問い合わせて、ベルト搬送・トゲ即死・ポータル移動・爆弾起爆を駆動する。
	//==============================

	/// <summary>ステージにギミックが 1 つでも有るか。</summary>
	bool HasGimmicks() const { return !gimmicks_.empty(); }

	/// <summary>中心 center・半サイズ half の AABB の足元（底面の 1 セル下）の横帯を走査し、
	/// どこか 1 マスでもベルトコンベアなら流れる向き（-1=左 / +1=右）を返す。無ければ 0。
	/// edgeMargin だけ左右を内側へ詰めてから走査する（爪先だけ掛かった状態を除外したいときは正の値、
	/// 足がわずかでも触れていれば拾いたいときは 0 か負の値）。</summary>
	int BeltDirUnderAabb(const Vector3& center, const Vector3& half, float edgeMargin) const;

	/// <summary>中心 center・半サイズ half の AABB がトゲ（32）のセルと重なっているか（＝即死）。</summary>
	bool OverlapsSpike(const Vector3& center, const Vector3& half) const;

	/// <summary>中心 center・半サイズ half の AABB がいずれかのポータルセルと重なっていれば true を返し、
	/// 出口ワールド座標を outDest に入れる。出口は「重なっていない」ポータルからランダムに 1 つ。
	/// 重なっていないポータルが 1 つも無ければ（＝全ポータルに跨っている / 単独ポータル）false。
	/// 「出るまで再ワープしない」制御は呼び出し側（GameScene）が行う。</summary>
	bool TryPortal(const Vector3& center, const Vector3& half, Vector3& outDest);

	/// <summary>中心 center・半径 radius の球に重なる爆弾ブロックの信管を開始する（既に作動中なら無視）。
	/// 近接攻撃・弾の着弾・爆発など「攻撃が当たった」あらゆる箇所から呼ぶ。</summary>
	void ArmBombsInSphere(const Vector3& center, float radius);

	/// <summary>前フレーム以降に起爆した爆弾の一覧を取り出す（取り出すと空になる）。</summary>
	std::vector<BombExplosion> ConsumeBombExplosions();

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
	bool  SegmentHitsSolid(const Vector3& a, const Vector3& b) const override;
	bool  IsPrecariousBreakableFloor(const Vector3& pos) const override;

private:
	struct Tile {
		int cx = 0;
		int cy = 0;
		int value = 0;        // 元の CSV 値（種別・テクスチャ選択に使う）
		bool destroyed = false;
		float hp = 0.0f;      // 壊れる床のみ意味を持つ
		std::unique_ptr<PrimitiveInstance> visual;
	};

	// ギミック 1 マス分（30 番台）。ベルト／爆弾／ポータルは仮ボックス（visual）、トゲは Spike.mesh（model）。
	struct Gimmick {
		int cx = 0;
		int cy = 0;
		GimmickType type = GimmickType::None;
		bool destroyed = false; // 爆弾が起爆したあと（描画・判定から外す）
		float fuse = -1.0f;     // 爆弾: 0 以上なら作動中でカウントダウン。-1 で未作動
		std::unique_ptr<PrimitiveInstance> visual;
		std::unique_ptr<Object3DInstance> model;
	};

	void ExtractSpawns();     // 1/2 を拾ってワールド座標に変換し、地形値を 0 に落とす
	void BuildTilesAndGimmicks();
	GimmickType GimmickTypeAtCell(int cx, int cy) const;
	void DetonateBomb(Gimmick& bomb);

	Camera* camera_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;

	int cells_[kRows][kCols] = {};         // 元データ（1/2 は 0 に落とし済み）
	int tileIndex_[kRows][kCols] = {};     // tiles_ への添字。-1 で「見た目なし」
	int gimmickIndex_[kRows][kCols] = {};  // gimmicks_ への添字。-1 で「ギミックなし」

	std::vector<Tile> tiles_;              // 見た目を持つセル（10/20 系）のみ
	std::vector<Gimmick> gimmicks_;        // ギミック（30 番台）のみ
	std::vector<BombExplosion> pendingBombExplosions_;

	bool hasPlayerSpawn_ = false;
	Vector3 playerSpawnWorld_{};
	std::vector<Vector3> enemySpawnsWorld_;
};
