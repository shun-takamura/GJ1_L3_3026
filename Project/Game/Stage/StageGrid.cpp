#include "Stage/StageGrid.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

#include "Camera.h"
#include "Log.h"
#include "Primitive/PrimitiveInstance.h"
#include "PrimitivePipeline.h"
#include "Vector4.h"

namespace {
	// 種別コード（値 / 10）
	constexpr int kKindUnbreakable = 1; // 10-19
	constexpr int kKindBreakable = 2;   // 20-29

	// 文字列の前後空白を落とす（CSV セルの余分なスペース対策）。
	std::string Trim(const std::string& s) {
		size_t b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos) return {};
		size_t e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
	}
}

// PrimitiveInstance を unique_ptr で持つ Tile を vector で抱えるため、
// コンストラクタ／デストラクタは cpp 側に出す（incomplete type の破棄エラー回避）。
StageGrid::StageGrid() = default;
StageGrid::~StageGrid() = default;

bool StageGrid::LoadFromCsv(const std::string& path) {
	for (auto& row : cells_) {
		for (int& v : row) v = 0;
	}

	std::ifstream ifs(path);
	if (!ifs) {
		Log("StageGrid: CSV を開けません: " + path + " -> フォールバック床を使用\n");
		for (int cx = 0; cx < kCols; ++cx) {
			cells_[kRows - 1][cx] = 10;
		}
		ExtractSpawns();
		return false;
	}

	std::string line;
	int row = 0;
	while (row < kRows && std::getline(ifs, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		int col = 0;
		size_t start = 0;
		while (col < kCols) {
			size_t comma = line.find(',', start);
			std::string tok = Trim(comma == std::string::npos
				? line.substr(start)
				: line.substr(start, comma - start));
			cells_[row][col] = tok.empty() ? 0 : std::atoi(tok.c_str());
			++col;
			if (comma == std::string::npos) break;
			start = comma + 1;
		}
		if (col < kCols) {
			Log("StageGrid: 行 " + std::to_string(row) + " の列数が不足。0 埋めしました\n");
		}
		++row;
	}
	if (row < kRows) {
		Log("StageGrid: 行数が不足 (" + std::to_string(row) + "/" + std::to_string(kRows)
			+ ")。残りは 0 埋めしました\n");
	}

	ExtractSpawns();
	return true;
}

void StageGrid::ExtractSpawns() {
	hasPlayerSpawn_ = false;
	playerSpawnWorld_ = {};
	enemySpawnsWorld_.clear();

	for (int cy = 0; cy < kRows; ++cy) {
		for (int cx = 0; cx < kCols; ++cx) {
			const int v = cells_[cy][cx];
			if (v == 1) {
				playerSpawnWorld_ = CellToWorldCenter(cx, cy);
				hasPlayerSpawn_ = true;
				cells_[cy][cx] = 0; // キャラのマスに地形は同時に置かない（仕様書 5 節）
			} else if (v == 2) {
				enemySpawnsWorld_.push_back(CellToWorldCenter(cx, cy));
				cells_[cy][cx] = 0;
			}
		}
	}
}

void StageGrid::Initialize(Camera* camera) {
	camera_ = camera;

	tiles_.clear();
	for (auto& r : tileIndex_) {
		for (int& i : r) i = -1;
	}

	for (int cy = 0; cy < kRows; ++cy) {
		for (int cx = 0; cx < kCols; ++cx) {
			const int v = cells_[cy][cx];
			const int kind = v / 10;
			if (kind != kKindUnbreakable && kind != kKindBreakable) {
				continue; // 見た目を持つのは今日のところ床のみ（ギミックは未実装）
			}

			Tile t;
			t.cx = cx;
			t.cy = cy;
			t.value = v;
			t.hp = (kind == kKindBreakable) ? kBreakableHP : 0.0f;

			t.visual = std::make_unique<PrimitiveInstance>();
			t.visual->Initialize(PrimitiveInstance::PrimitiveType::Box,
				"Tile_" + std::to_string(cx) + "_" + std::to_string(cy));
			t.visual->SetCamera(camera_);
			t.visual->SetScale({ kCellSize, kCellSize, kCellSize });
			t.visual->SetTranslate(CellToWorldCenter(cx, cy));

			// 既定は加算ブレンド＋深度書き込み無しなので、不透明タイル用に明示する
			// （加算だと黒 = {0,0,0} が背景に埋もれて見えない）。
			PrimitiveMesh& mesh = t.visual->GetMesh();
			mesh.SetBlendMode(PrimitivePipeline::kBlendModeNormal);
			mesh.SetDepthWrite(true);
			mesh.SetCullBackface(true);
			mesh.SetColor(kind == kKindUnbreakable
				? Vector4{ 0.0f, 0.0f, 0.0f, 1.0f }   // 壊れない床 = 黒
				: Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }); // 壊れる床   = 白

			tileIndex_[cy][cx] = static_cast<int>(tiles_.size());
			tiles_.push_back(std::move(t));
		}
	}
}

void StageGrid::Finalize() {
	tiles_.clear();
}

void StageGrid::Update() {
	for (auto& t : tiles_) {
		if (!t.destroyed && t.visual) {
			t.visual->Update();
		}
	}
}

void StageGrid::Draw() {
	for (auto& t : tiles_) {
		if (!t.destroyed && t.visual) {
			t.visual->Draw();
		}
	}
}

int StageGrid::DamageSphere(const Vector3& center, float radius, float damage) {
	int broke = 0;
	const float half = kCellSize * 0.5f;
	const float r2 = radius * radius;

	for (auto& t : tiles_) {
		if (t.destroyed || t.value / 10 != kKindBreakable) {
			continue;
		}
		const Vector3 c = CellToWorldCenter(t.cx, t.cy);
		// 球 vs セル AABB の最短距離二乗。
		// <windows.h> の max マクロ回避のため関数名を括弧で包む。
		const float dx = (std::max)(std::fabs(center.x - c.x) - half, 0.0f);
		const float dy = (std::max)(std::fabs(center.y - c.y) - half, 0.0f);
		const float dz = (std::max)(std::fabs(center.z - c.z) - half, 0.0f);
		if (dx * dx + dy * dy + dz * dz > r2) {
			continue;
		}

		t.hp -= damage;
		if (t.hp <= 0.0f) {
			t.destroyed = true;
			++broke;
		} else if (t.visual) {
			// 破壊されるまでは見た目の変化が無く「本当にダメージが通っているのか」が
			// 分かりにくいので、残りHPの割合ぶん赤みを強くする(満タン=白 → 瀕死=赤)。
			const float ratio = (std::max)(t.hp / kBreakableHP, 0.0f);
			t.visual->GetMesh().SetColor({ 1.0f, ratio, ratio, 1.0f });
		}
	}
	return broke;
}

void StageGrid::ResetTerrain() {
	for (auto& t : tiles_) {
		if (t.value / 10 == kKindBreakable) {
			t.hp = kBreakableHP;
			if (t.visual) {
				t.visual->GetMesh().SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // ダメージ表示の赤みも元の白へ戻す
			}
		}
		t.destroyed = false;
	}
}

int StageGrid::GetChip(int cx, int cy) const {
	if (!InBounds(cx, cy)) {
		return 0;
	}
	const int idx = tileIndex_[cy][cx];
	if (idx >= 0 && idx < static_cast<int>(tiles_.size()) && tiles_[idx].destroyed) {
		return 0;
	}
	return cells_[cy][cx];
}

bool StageGrid::IsSolidCell(int cx, int cy) const {
	const int kind = GetChip(cx, cy) / 10;
	return kind == kKindUnbreakable || kind == kKindBreakable;
}

bool StageGrid::IsBreakableCell(int cx, int cy) const {
	return GetChip(cx, cy) / 10 == kKindBreakable;
}

Vector3 StageGrid::CellToWorldCenter(int cx, int cy) const {
	// X: ステージ中央を x=0 にそろえる。Y: CSV 上行ほど上（Y-up へ反転）。
	const float x = (static_cast<float>(cx) + 0.5f - kCols * 0.5f) * kCellSize;
	const float y = (static_cast<float>(kRows - 1 - cy) + 0.5f) * kCellSize;
	return { x, y, 0.0f };
}

void StageGrid::WorldToCell(const Vector3& p, int& cx, int& cy) const {
	cx = static_cast<int>(std::floor(p.x / kCellSize)) + kCols / 2;
	cy = (kRows - 1) - static_cast<int>(std::floor(p.y / kCellSize));
}

bool StageGrid::IsPointInsideBounds(const Vector3& p) const {
	const float halfW = kCols * 0.5f * kCellSize;
	// 左右の外＝場外。床の穴から落ちた場合は y が下限を割ったところで場外。
	// 上方向（ジャンプで高く飛ぶ）は場外にしない。
	return p.x > -(halfW + 1.0f) && p.x < (halfW + 1.0f) && p.y > -1.0f;
}

bool StageGrid::OverlapsSolid(const Vector3& center, const Vector3& half) const {
	const float cellHalf = kCellSize * 0.5f;
	int cxLo, cyLo, cxHi, cyHi;
	WorldToCell({ center.x - half.x, center.y + half.y, 0.0f }, cxLo, cyLo);
	WorldToCell({ center.x + half.x, center.y - half.y, 0.0f }, cxHi, cyHi);
	for (int cy = cyLo; cy <= cyHi; ++cy) {
		for (int cx = cxLo; cx <= cxHi; ++cx) {
			if (!IsSolidCell(cx, cy)) continue;
			const Vector3 c = CellToWorldCenter(cx, cy);
			if (center.x + half.x > c.x - cellHalf && center.x - half.x < c.x + cellHalf &&
				center.y + half.y > c.y - cellHalf && center.y - half.y < c.y + cellHalf) {
				return true;
			}
		}
	}
	return false;
}

bool StageGrid::SegmentHitsSolid(const Vector3& a, const Vector3& b) const {
	// 線分を CellSize の半分ぶんずつサンプリングして、solid セルを踏んでいないか調べる。
	// 射線チェック用途なので、DDA のような厳密なグリッド走査までは要らない（1マス未満の
	// すり抜けは実用上問題にならない）。
	const float dx = b.x - a.x;
	const float dy = b.y - a.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len < 1e-4f) {
		int cx, cy;
		WorldToCell(a, cx, cy);
		return IsSolidCell(cx, cy);
	}
	const float step = kCellSize * 0.5f;
	const int steps = static_cast<int>(len / step) + 1;
	for (int i = 0; i <= steps; ++i) {
		const float t = static_cast<float>(i) / static_cast<float>(steps);
		const Vector3 p{ a.x + dx * t, a.y + dy * t, 0.0f };
		int cx, cy;
		WorldToCell(p, cx, cy);
		if (IsSolidCell(cx, cy)) {
			return true;
		}
	}
	return false;
}

StageMoveResult StageGrid::MoveAabb(const Vector3& from, const Vector3& to, const Vector3& half) const {
	StageMoveResult result;
	Vector3 pos = from;
	const float eps = 0.001f;
	const float cellHalf = kCellSize * 0.5f;

	// ---- X 軸 ----
	pos.x = to.x;
	{
		const float dir = to.x - from.x;
		int cxLo, cyLo, cxHi, cyHi;
		WorldToCell({ pos.x - half.x, pos.y + half.y, 0.0f }, cxLo, cyLo); // 左上
		WorldToCell({ pos.x + half.x, pos.y - half.y, 0.0f }, cxHi, cyHi); // 右下
		for (int cy = cyLo; cy <= cyHi; ++cy) {
			for (int cx = cxLo; cx <= cxHi; ++cx) {
				if (!IsSolidCell(cx, cy)) continue;
				const Vector3 c = CellToWorldCenter(cx, cy);
				// Y 方向に実際に重なっている行だけを壁として扱う。
				if (pos.y + half.y <= c.y - cellHalf + eps) continue;
				if (pos.y - half.y >= c.y + cellHalf - eps) continue;
				const float aMinX = pos.x - half.x;
				const float aMaxX = pos.x + half.x;
				if (aMaxX <= c.x - cellHalf || aMinX >= c.x + cellHalf) continue;
				if (dir > 0.0f) { pos.x = c.x - cellHalf - half.x - eps; result.hitWall = true; }
				else if (dir < 0.0f) { pos.x = c.x + cellHalf + half.x + eps; result.hitWall = true; }
			}
		}
	}

	// ---- Y 軸 ----
	pos.y = to.y;
	{
		const float dir = to.y - from.y;
		int cxLo, cyLo, cxHi, cyHi;
		WorldToCell({ pos.x - half.x, pos.y + half.y, 0.0f }, cxLo, cyLo);
		WorldToCell({ pos.x + half.x, pos.y - half.y, 0.0f }, cxHi, cyHi);
		for (int cy = cyLo; cy <= cyHi; ++cy) {
			for (int cx = cxLo; cx <= cxHi; ++cx) {
				if (!IsSolidCell(cx, cy)) continue;
				const Vector3 c = CellToWorldCenter(cx, cy);
				if (pos.x + half.x <= c.x - cellHalf + eps) continue;
				if (pos.x - half.x >= c.x + cellHalf - eps) continue;
				const float aMinY = pos.y - half.y;
				const float aMaxY = pos.y + half.y;
				if (aMaxY <= c.y - cellHalf || aMinY >= c.y + cellHalf) continue;
				if (dir > 0.0f) { pos.y = c.y - cellHalf - half.y - eps; result.hitCeiling = true; }
				else { pos.y = c.y + cellHalf + half.y + eps; result.grounded = true; }
			}
		}
	}

	// ---- 静止時の接地プローブ（真下にわずかに伸ばして床があるか） ----
	if (!result.grounded) {
		int cxLo, cyLo, cxHi, cyHi;
		WorldToCell({ pos.x - half.x + eps, pos.y - half.y - 0.05f, 0.0f }, cxLo, cyLo);
		WorldToCell({ pos.x + half.x - eps, pos.y - half.y - 0.05f, 0.0f }, cxHi, cyHi);
		for (int cy = cyLo; cy <= cyHi && !result.grounded; ++cy) {
			for (int cx = cxLo; cx <= cxHi && !result.grounded; ++cx) {
				if (IsSolidCell(cx, cy)) result.grounded = true;
			}
		}
	}

	result.position = pos;
	return result;
}
