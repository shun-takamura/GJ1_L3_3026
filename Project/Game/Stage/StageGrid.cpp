#include "Stage/StageGrid.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

#include "Camera.h"
#include "Log.h"
#include "Primitive/PrimitiveInstance.h"
#include "PrimitivePipeline.h"
#include "Object3DInstance.h"
#include "RandomGenerator.h"
#include "Vector4.h"

namespace {
	// 種別コード（値 / 10）
	constexpr int kKindUnbreakable = 1; // 10-19
	constexpr int kKindBreakable = 2;   // 20-29
	constexpr int kKindGimmick = 3;     // 30-39

	// ギミックの仮ボックス色（本番モデルが入るまでの目印）。
	const Vector4 kColorBeltLeft { 0.15f, 0.35f, 0.95f, 1.0f }; // 左ベルト = 青
	const Vector4 kColorBeltRight{ 0.95f, 0.85f, 0.10f, 1.0f }; // 右ベルト = 黄
	const Vector4 kColorBomb     { 0.95f, 0.12f, 0.10f, 1.0f }; // 爆弾ブロック = 赤
	const Vector4 kColorPortal   { 0.65f, 0.20f, 0.90f, 1.0f }; // ポータル = 紫

	StageGrid::GimmickType GimmickFromValue(int value) {
		switch (value % 10) {
		case 0: return StageGrid::GimmickType::BeltLeft;
		case 1: return StageGrid::GimmickType::BeltRight;
		case 2: return StageGrid::GimmickType::Spike;
		case 3: return StageGrid::GimmickType::Bomb;
		case 4: return StageGrid::GimmickType::Portal;
		default: return StageGrid::GimmickType::None;
		}
	}

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

void StageGrid::Initialize(Camera* camera, Object3DManager* object3DManager, DirectXCore* dxCore) {
	camera_ = camera;
	object3DManager_ = object3DManager;
	dxCore_ = dxCore;
	BuildTilesAndGimmicks();
}

void StageGrid::BuildTilesAndGimmicks() {
	tiles_.clear();
	gimmicks_.clear();
	pendingBombExplosions_.clear();
	for (auto& r : tileIndex_) {
		for (int& i : r) i = -1;
	}
	for (auto& r : gimmickIndex_) {
		for (int& i : r) i = -1;
	}

	for (int cy = 0; cy < kRows; ++cy) {
		for (int cx = 0; cx < kCols; ++cx) {
			const int v = cells_[cy][cx];
			const int kind = v / 10;

			if (kind == kKindUnbreakable || kind == kKindBreakable) {
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
				continue;
			}

			if (kind != kKindGimmick) {
				continue;
			}

			const GimmickType type = GimmickFromValue(v);
			if (type == GimmickType::None) {
				continue;
			}

			Gimmick g;
			g.cx = cx;
			g.cy = cy;
			g.type = type;
			const Vector3 pos = CellToWorldCenter(cx, cy);
			const std::string tag = std::to_string(cx) + "_" + std::to_string(cy);

			if (type == GimmickType::Spike) {
				// トゲだけは専用モデル（Spike.mesh）。描画コンテキスト未設定なら見た目なし（判定は生きる）。
				if (object3DManager_ && dxCore_) {
					g.model = std::make_unique<Object3DInstance>();
					g.model->Initialize(object3DManager_, dxCore_,
						"Resources/Models/StageGimmick", "Spike.mesh", "Spike_" + tag);
					g.model->SetCamera(camera_);
					g.model->SetScale({ kCellSize, kCellSize, kCellSize });
					g.model->SetTranslate(pos);
				}
			} else {
				// 左ベルト＝青 / 右ベルト＝黄 / 爆弾＝赤 / ポータル＝紫 の仮ボックス。
				Vector4 color = kColorPortal;
				if (type == GimmickType::BeltLeft)  color = kColorBeltLeft;
				if (type == GimmickType::BeltRight) color = kColorBeltRight;
				if (type == GimmickType::Bomb)      color = kColorBomb;

				g.visual = std::make_unique<PrimitiveInstance>();
				g.visual->Initialize(PrimitiveInstance::PrimitiveType::Box, "Gimmick_" + tag);
				g.visual->SetCamera(camera_);
				g.visual->SetScale({ kCellSize, kCellSize, kCellSize });
				g.visual->SetTranslate(pos);
				PrimitiveMesh& mesh = g.visual->GetMesh();
				mesh.SetBlendMode(PrimitivePipeline::kBlendModeNormal);
				mesh.SetDepthWrite(true);
				mesh.SetCullBackface(true);
				mesh.SetColor(color);

				// ベルトは tread テクスチャ + UV スクロールでキャタピラの回転を表現する
				// (スキニング不要。実際のスクロールは Update() で毎フレーム SetUVOffset)。
				if (type == GimmickType::BeltLeft || type == GimmickType::BeltRight) {
					mesh.SetTexture("Resources/Textures/Belt_Tread.dds");
					mesh.SetUVScale({ kBeltTilesU, 1.0f });
					// このメッシュ面では +U = 画面左。搬送方向へシェブロンが向くよう右ベルトで U 反転。
					mesh.SetUVFlipU(type == GimmickType::BeltRight);
				}
			}

			gimmickIndex_[cy][cx] = static_cast<int>(gimmicks_.size());
			gimmicks_.push_back(std::move(g));
		}
	}
}

void StageGrid::Finalize() {
	tiles_.clear();
	gimmicks_.clear();
	pendingBombExplosions_.clear();
}

void StageGrid::Update(float dt) {
	for (auto& t : tiles_) {
		if (!t.destroyed && t.visual) {
			t.visual->Update();
		}
	}

	// 爆弾の信管を進める。0 以下になったら起爆（DetonateBomb が誘爆と地形削りまで行う）。
	// range-for 中に誘爆で fuse を書き換えるだけなので vector の再確保は起きない。
	for (size_t i = 0; i < gimmicks_.size(); ++i) {
		Gimmick& g = gimmicks_[i];
		if (g.type != GimmickType::Bomb || g.destroyed) {
			continue;
		}
		if (g.fuse >= 0.0f) {
			g.fuse -= dt;
			if (g.fuse <= 0.0f) {
				DetonateBomb(g);
				continue;
			}
			// 起爆が近いほど速く赤⇔白で点滅させる。
			if (g.visual) {
				const float period = (std::max)(0.08f, g.fuse * 0.35f);
				const bool on = std::fmod(g.fuse, period) < period * 0.5f;
				g.visual->GetMesh().SetColor(on ? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f } : kColorBomb);
			}
		}
	}

	// ベルト tread の UV スクロール位相を進める。dt は GameScene 側で TimeGroup 済みなので、
	// ヒットストップ/スロー中はベルトの見た目も一緒に止まる(BeltShiftX の搬送量と歩調が合う)。
	beltUvOffset_ = std::fmod(beltUvOffset_ + kBeltUvSpeed * dt, 1.0f);

	for (auto& g : gimmicks_) {
		if (!g.destroyed && g.visual) {
			if (g.type == GimmickType::BeltLeft || g.type == GimmickType::BeltRight) {
				// +U = 画面左なので、右搬送(+X)はオフセットを減らす向き。
				const float dir = (g.type == GimmickType::BeltRight) ? -1.0f : 1.0f;
				g.visual->GetMesh().SetUVOffset({ dir * beltUvOffset_, 0.0f });
			}
			g.visual->Update();
		}
		if (!g.destroyed && g.model) {
			g.model->Update();
		}
	}
}

void StageGrid::Draw() {
	for (auto& t : tiles_) {
		if (!t.destroyed && t.visual) {
			t.visual->Draw();
		}
	}
	for (auto& g : gimmicks_) {
		if (!g.destroyed && g.visual) {
			g.visual->Draw();
		}
	}
}

void StageGrid::DrawModels(DirectXCore* dxCore) {
	for (auto& g : gimmicks_) {
		if (!g.destroyed && g.model) {
			g.model->Draw(dxCore);
		}
	}
}

StageGrid::GimmickType StageGrid::GimmickTypeAtCell(int cx, int cy) const {
	if (!InBounds(cx, cy)) {
		return GimmickType::None;
	}
	const int idx = gimmickIndex_[cy][cx];
	if (idx < 0 || idx >= static_cast<int>(gimmicks_.size()) || gimmicks_[idx].destroyed) {
		return GimmickType::None;
	}
	return gimmicks_[idx].type;
}

int StageGrid::BeltDirUnderAabb(const Vector3& center, const Vector3& half, float edgeMargin) const {
	const float footY = center.y - half.y - 0.05f; // 底面のすぐ下
	float inset = edgeMargin;
	if (inset > half.x - 0.02f) inset = half.x - 0.02f; // 帯が反転しないようにクランプ
	int cxLo, cy, cxHi, cyHi;
	WorldToCell({ center.x - half.x + inset, footY, 0.0f }, cxLo, cy);
	WorldToCell({ center.x + half.x - inset, footY, 0.0f }, cxHi, cyHi);
	for (int cx = cxLo; cx <= cxHi; ++cx) {
		switch (GimmickTypeAtCell(cx, cy)) {
		case GimmickType::BeltLeft:  return -1;
		case GimmickType::BeltRight: return 1;
		default: break;
		}
	}
	return 0;
}

bool StageGrid::OverlapsSpike(const Vector3& center, const Vector3& half) const {
	int cxLo, cyLo, cxHi, cyHi;
	WorldToCell({ center.x - half.x, center.y + half.y, 0.0f }, cxLo, cyLo);
	WorldToCell({ center.x + half.x, center.y - half.y, 0.0f }, cxHi, cyHi);
	for (int cy = cyLo; cy <= cyHi; ++cy) {
		for (int cx = cxLo; cx <= cxHi; ++cx) {
			if (GimmickTypeAtCell(cx, cy) == GimmickType::Spike) {
				return true;
			}
		}
	}
	return false;
}

bool StageGrid::TryPortal(const Vector3& center, const Vector3& half, Vector3& outDest) {
	int cxLo, cyLo, cxHi, cyHi;
	WorldToCell({ center.x - half.x, center.y + half.y, 0.0f }, cxLo, cyLo);
	WorldToCell({ center.x + half.x, center.y - half.y, 0.0f }, cxHi, cyHi);
	auto overlapsBox = [&](int gx, int gy) {
		return gx >= cxLo && gx <= cxHi && gy >= cyLo && gy <= cyHi;
	};

	bool onPortal = false;
	std::vector<const Gimmick*> exits;
	for (const auto& g : gimmicks_) {
		if (g.type != GimmickType::Portal || g.destroyed) {
			continue;
		}
		if (overlapsBox(g.cx, g.cy)) {
			onPortal = true;      // 今キャラが乗っているポータル
		} else {
			exits.push_back(&g);  // 出口候補
		}
	}
	if (!onPortal || exits.empty()) {
		return false;
	}
	const int pick = RandomGenerator::Instance().NextInt(0, static_cast<int>(exits.size()) - 1);
	outDest = CellToWorldCenter(exits[pick]->cx, exits[pick]->cy);
	return true;
}

void StageGrid::ArmBombsInSphere(const Vector3& center, float radius) {
	const float half = kCellSize * 0.5f;
	const float r2 = radius * radius;
	for (auto& g : gimmicks_) {
		if (g.type != GimmickType::Bomb || g.destroyed || g.fuse >= 0.0f) {
			continue; // 未作動の爆弾だけを対象にする（作動中は上書きしない）
		}
		const Vector3 c = CellToWorldCenter(g.cx, g.cy);
		const float dx = (std::max)(std::fabs(center.x - c.x) - half, 0.0f);
		const float dy = (std::max)(std::fabs(center.y - c.y) - half, 0.0f);
		if (dx * dx + dy * dy <= r2) {
			g.fuse = kBombFuseSeconds;
		}
	}
}

void StageGrid::DetonateBomb(Gimmick& bomb) {
	bomb.destroyed = true;
	bomb.fuse = -1.0f;
	gimmickIndex_[bomb.cy][bomb.cx] = -1;

	const Vector3 center = CellToWorldCenter(bomb.cx, bomb.cy);
	const float radius = kBombRadiusCells * kCellSize;

	// 壊れる床を爆風半径ぶん削る（爆弾ブロックで壊れた床はラウンドリセットでも復活しない）。
	DamageSphere(center, radius, kBombDamage, /*permanent=*/true);

	// キャラへの適用は GameScene 側（吹っ飛ばしはリアルな放射方向で）。
	pendingBombExplosions_.push_back({ center, radius, kBombDamage });

	// 誘爆: 範囲内の未作動の爆弾に短い信管を仕込む。
	for (auto& g : gimmicks_) {
		if (g.type != GimmickType::Bomb || g.destroyed || g.fuse >= 0.0f) {
			continue;
		}
		const Vector3 c = CellToWorldCenter(g.cx, g.cy);
		const float dx = c.x - center.x;
		const float dy = c.y - center.y;
		if (dx * dx + dy * dy <= radius * radius) {
			g.fuse = kBombChainFuseSeconds;
		}
	}
}

std::vector<StageGrid::BombExplosion> StageGrid::ConsumeBombExplosions() {
	std::vector<BombExplosion> out = std::move(pendingBombExplosions_);
	pendingBombExplosions_.clear();
	return out;
}

int StageGrid::DamageSphere(const Vector3& center, float radius, float damage, bool permanent) {
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
			if (permanent) {
				t.permanentlyDestroyed = true; // ResetTerrain でも復活させない
			}
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
		if (t.permanentlyDestroyed) {
			continue; // 爆弾ブロックの爆風で壊れた床は復活させない
		}
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
	if (kind == kKindUnbreakable || kind == kKindBreakable) {
		return true;
	}
	// ベルトコンベア（30/31）は乗れるように、爆弾ブロック（33）は通常ブロックと同じく
	// 床・壁として扱う（起爆して destroyed になった時点で GimmickTypeAtCell が None を返し非ソリッドへ）。
	// トゲ・ポータルはすり抜ける（非ソリッド）。
	const GimmickType g = GimmickTypeAtCell(cx, cy);
	return g == GimmickType::BeltLeft || g == GimmickType::BeltRight || g == GimmickType::Bomb;
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

bool StageGrid::IsPrecariousBreakableFloor(const Vector3& pos) const {
	// pos の足元は「キャラ中心の1マス下」あたり。そこが壊れる床で、
	// さらにその下（2マス下）に何も無ければ、撃たれると落下する危うい足場。
	int cx, cy;
	WorldToCell(pos, cx, cy);
	const int belowY = cy + 1;      // ワールドで下（WorldToCell は y 下ほど cy 大）
	const int below2Y = cy + 2;
	if (!IsBreakableCell(cx, belowY)) {
		return false;
	}
	return !IsSolidCell(cx, below2Y);
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
