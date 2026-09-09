#include "GameScene.h"

#include "SceneManager.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "MouseInput.h"
#include "ControllerInput.h"
#include "Object3DManager.h"
#include "LightManager.h"
#include "DirectXCore.h"
#include "TextRenderer.h"
#include "WindowsApplication.h"
#include "TimeGroup.h"
#include "Physics/CollisionSystem.h"
#include "Primitive/DebugDraw.h"
#include "Primitive/LineRenderer.h"
#include "MathUtility.h"
#include "Matrix4x4.h"
#include "Vector2.h"
#include "RandomGenerator.h"
#include "ImGuiManager.h"
#include "ViewportWindow.h"
#include "Weapon/Weapon.h"
#include "Weapon/Pistol.h"
#include "Weapon/AssaultRifle.h"
#include "Weapon/Shotgun.h"
#include "Weapon/Blaster.h"
#include "Weapon/GrenadeLauncher.h"
#include "Weapon/SniperRifle.h"
#include "Weapon/Minigun.h"
#include "Weapon/HandCannon.h"
#include "Weapon/RicochetRifle.h"
#include "Weapon/IceGun.h"
#include "Weapon/FireGun.h"
#include "Weapon/FireHazard.h"
#include "Sound/SoundManager.h"
#include "GameApp.h"
#include "Match/MatchResultRelay.h"
#include "Match/MatchScoreText.h"
#include "Save/SaveData.h"
#include "Effect/EffectManager.h"
#include "Log.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <dinput.h>
#include <Xinput.h>

namespace {
	/// <summary>
	/// マウスのウィンドウ内座標(MouseInput::GetClientX/Y)から、NDC座標(-1〜1)を計算する。
	///
	/// Debug構成では ImGui のドッキングエディタが立ち上がり、ゲーム画面はウィンドウ全体では
	/// なく「Scene」パネルの中の画像領域に描画される(10_Editor.md)。ウィンドウ全体を
	/// そのままゲーム画面とみなして変換すると、パネルの位置・サイズぶんだけ照準がずれてしまう。
	/// そのため `ImGuiManager::GetViewportWindow()` からその画像領域の実際のスクリーン座標を
	/// 取得できる場合はそちらを基準にし、無い場合(Release等、エディタが存在しない構成)だけ
	/// ウィンドウ全体(内部描画解像度 kClientWidth/Height)を基準にする。
	/// </summary>
	Vector2 ComputeMouseNdc(LONG mouseClientX, LONG mouseClientY) {
		ViewportWindow* viewport = ImGuiManager::Instance().GetViewportWindow();
		if (viewport && viewport->GetImageScreenSize().x > 0.0f && viewport->GetImageScreenSize().y > 0.0f) {
			const ImVec2 imgPos = viewport->GetImageScreenPos();
			const ImVec2 imgSize = viewport->GetImageScreenSize();
			const float u = (static_cast<float>(mouseClientX) - imgPos.x) / imgSize.x;
			const float v = (static_cast<float>(mouseClientY) - imgPos.y) / imgSize.y;
			return { u * 2.0f - 1.0f, 1.0f - v * 2.0f }; // スクリーンYは下向き正、NDCのYは上向き正なので反転
		}
		const float u = static_cast<float>(mouseClientX) / static_cast<float>(WindowsApplication::kClientWidth);
		const float v = static_cast<float>(mouseClientY) / static_cast<float>(WindowsApplication::kClientHeight);
		return { u * 2.0f - 1.0f, 1.0f - v * 2.0f };
	}

	/// <summary>
	/// NDC座標から、ownerPos を通る「照準方向」を計算する。
	///
	/// カメラは固定視点(ズームなし)だがパースペクティブ投影なので、素朴に
	/// スクリーン座標をワールドXYへ引き伸ばすことはできない。そこで一般的な
	/// mouse picking の手順を踏む: NDC座標→view-projectionの逆行列で
	/// ニア/ファークリップ上の2点をワールドへ戻す→その2点を結ぶレイと
	/// 「ownerPos と同じZ平面」(全キャラ・全弾がこの平面上にいる)との交点を求める。
	/// </summary>
	/// <param name="outWorldPoint">計算に使ったワールド上の交点(デバッグ描画用。不要なら nullptr)</param>
	Vector2 ComputeAimDirectionFromNdc(const Camera& camera, float ndcX, float ndcY,
		const Vector3& ownerPos, Vector3* outWorldPoint) {
		// view-projection の逆行列で、ニアクリップ面・ファークリップ面上の点をワールド座標へ戻す。
		// TransformCoordinate は同次座標のw除算(パースペクティブ分割)込みの変換。
		const Matrix4x4 invViewProj = Inverse(camera.GetViewProjectionMatrix());
		const Vector3 nearPoint = TransformCoordinate({ ndcX, ndcY, 0.0f }, invViewProj);
		const Vector3 farPoint = TransformCoordinate({ ndcX, ndcY, 1.0f }, invViewProj);

		Vector3 rayDir = { farPoint.x - nearPoint.x, farPoint.y - nearPoint.y, farPoint.z - nearPoint.z };
		const float rayLen = Length(rayDir);
		if (rayLen > 0.0001f) {
			rayDir = { rayDir.x / rayLen, rayDir.y / rayLen, rayDir.z / rayLen };
		}

		// レイと「ownerPos と同じZ平面」の交点(全キャラ・全弾は Z 固定平面上にいる想定)。
		Vector3 worldPoint = nearPoint;
		if (std::fabs(rayDir.z) > 0.0001f) {
			const float t = (ownerPos.z - nearPoint.z) / rayDir.z;
			worldPoint = { nearPoint.x + rayDir.x * t, nearPoint.y + rayDir.y * t, nearPoint.z + rayDir.z * t };
		}
		if (outWorldPoint) {
			*outWorldPoint = worldPoint;
		}

		Vector2 aim{ worldPoint.x - ownerPos.x, worldPoint.y - ownerPos.y };
		const float aimLen = std::sqrt(aim.x * aim.x + aim.y * aim.y);
		if (aimLen > 0.0001f) {
			aim.x /= aimLen;
			aim.y /= aimLen;
		} else {
			// カーソルがちょうどキャラの真上にある等、方向が定まらない場合のフォールバック。
			aim = { 1.0f, 0.0f };
		}
		return aim;
	}

	/// <summary>
	/// ランダムスポーンの候補になる武器1種ぶんのエントリ。「武器名の表示」「ランダム抽選」
	/// 「実際に1つ生成する」の3つを1つのテーブルにまとめておくことで、CreateRandomWeapon() と
	/// デバッグ用ImGuiチェックボックス(GameScene::Initialize の Weapon Tuning ウィンドウ)が
	/// 同じ並び・同じ有効/無効状態を共有できるようにしてある(スイッチ文とチェックボックスの
	/// 並びを別々に手で同期させると順序がズレるバグの元になるため)。
	/// </summary>
	struct WeaponSpawnEntry {
		const char* name;
		bool enabled; // false にすると CreateRandomWeapon() の抽選候補から外れる(デバッグ用)
		std::unique_ptr<Weapon>(*factory)();
	};

	WeaponSpawnEntry g_weaponSpawnPool[] = {
		{ "Pistol",        true, []() -> std::unique_ptr<Weapon> { return std::make_unique<Pistol>(); } },
		{ "AssaultRifle",  true, []() -> std::unique_ptr<Weapon> { return std::make_unique<AssaultRifle>(); } },
		{ "Shotgun",       true, []() -> std::unique_ptr<Weapon> { return std::make_unique<Shotgun>(); } },
		{ "Blaster",       true, []() -> std::unique_ptr<Weapon> { return std::make_unique<Blaster>(); } },
		{ "GrenadeLauncher", true, []() -> std::unique_ptr<Weapon> { return std::make_unique<GrenadeLauncher>(); } },
		{ "SniperRifle",   true, []() -> std::unique_ptr<Weapon> { return std::make_unique<SniperRifle>(); } },
		{ "Minigun",       true, []() -> std::unique_ptr<Weapon> { return std::make_unique<Minigun>(); } },
		{ "HandCannon",    true, []() -> std::unique_ptr<Weapon> { return std::make_unique<HandCannon>(); } },
		{ "RicochetRifle", true, []() -> std::unique_ptr<Weapon> { return std::make_unique<RicochetRifle>(); } },
		{ "IceGun",        true, []() -> std::unique_ptr<Weapon> { return std::make_unique<IceGun>(); } },
		{ "FireGun",       true, []() -> std::unique_ptr<Weapon> { return std::make_unique<FireGun>(); } },
	};
	constexpr int kWeaponSpawnPoolCount = sizeof(g_weaponSpawnPool) / sizeof(g_weaponSpawnPool[0]);

	/// <summary>g_weaponSpawnPool のうち enabled==true のものだけから乱数で1つ選び、フル装弾で
	/// 生成する。RandomGenerator 経由なのでリプレイのシード再現性を壊さない(11_Utilities.md、
	/// 生の rand() は使わない)。全て無効化されていた場合は Pistol にフォールバックする
	/// (呼び出し側は常に非nullptrを前提にしているため、nullptrは返さない)。</summary>
	std::unique_ptr<Weapon> CreateRandomWeapon() {
		int enabledIndices[kWeaponSpawnPoolCount];
		int enabledCount = 0;
		for (int i = 0; i < kWeaponSpawnPoolCount; ++i) {
			if (g_weaponSpawnPool[i].enabled) {
				enabledIndices[enabledCount++] = i;
			}
		}
		if (enabledCount == 0) {
			Log("CreateRandomWeapon: 武器が全てチェックOFFになっているため Pistol にフォールバックします\n");
			return std::make_unique<Pistol>();
		}
		const int pick = enabledIndices[RandomGenerator::Instance().NextInt(0, enabledCount - 1)];
		return g_weaponSpawnPool[pick].factory();
	}

	/// <summary>WeaponPickup を置ける床のあるマス(「自分のマスは空き、真下は地形」)を
	/// stage 全体から集め、その中からランダムに1つを *outPos へ書き出す。UpdateWeaponSpawner
	/// (ランダム武器の定期スポーン)と SpawnSpecificWeaponPickup(デバッグの特定武器スポーン)
	/// で候補地探索ロジックを重複させないための共通ヘルパー。置ける場所が1つも無ければ
	/// false を返す(足場のあるステージでは通常起きないが念のため)。</summary>
	// SE/BareHands/ に置いてある素手パンチのバリエーションのうち、実際に使うものだけを絞って
	// ある(ユーザー指定)。ResolveAttack が「実際に何かに当たった(敵 or 壊れる床)ときだけ」
	// 鳴らす ── 振っただけで外れたときは鳴らさない。
	const char* const kPunchSoundNames[] = {
		"Punch_Big", "Punch_Heavy1", "Punch_Light1", "Punch_Light2",
	};
	constexpr int kPunchSoundCount = sizeof(kPunchSoundNames) / sizeof(kPunchSoundNames[0]);

	bool PickWeaponSpawnPosition(const StageGrid& stage, Vector3* outPos) {
		std::vector<Vector3> candidates;
		for (int cy = 0; cy < StageGrid::kRows - 1; ++cy) {
			for (int cx = 0; cx < StageGrid::kCols; ++cx) {
				if (!stage.IsSolidCell(cx, cy) && stage.IsSolidCell(cx, cy + 1)) {
					candidates.push_back(stage.CellToWorldCenter(cx, cy));
				}
			}
		}
		if (candidates.empty()) {
			return false;
		}
		auto& rng = RandomGenerator::Instance();
		*outPos = candidates[static_cast<size_t>(rng.NextInt(0, static_cast<int>(candidates.size()) - 1))];
		return true;
	}
}

GameScene* GameScene::s_activeForDebug_ = nullptr;

int GameScene::PickStartStageIndex() {
	if (attractMode_) {
		// アトラクト(デモ)は常に Sample ステージ。名前に "Sample" を含む最初のものを使う。
		for (int i = 0; i < stageCatalog_.Count(); ++i) {
			if (stageCatalog_.NameAt(i).find("Sample") != std::string::npos) {
				return i;
			}
		}
		return 0; // 見つからなければ先頭
	}
	if (tutorialMode_) {
		// チュートリアルは専用ステージ固定。名前に "Tutorial" を含む最初のものを使う。
		for (int i = 0; i < stageCatalog_.Count(); ++i) {
			if (stageCatalog_.NameAt(i).find("Tutorial") != std::string::npos) {
				return i;
			}
		}
		Log("GameScene: Stage_Tutorial が見つかりません -> 先頭のステージで代用します\n");
		return 0;
	}
	// 本編は Sample を除いたシャッフルバッグ抽選。1 セット(どちらか 10 点先取)が終わるまで
	// 同じステージを引かない。バッグの仕切り直しは ResetBattleRotation()（セット決着時に呼ぶ）。
	return stageCatalog_.PickNextBattleIndex();
}

void GameScene::Initialize() {
	s_activeForDebug_ = this;

	//===================================
	// カメラ
	//===================================
	// 32x18 セルのステージ全体を横から見る固定視点。値は仮置きで、
	// デバッグカメラ(Scene 基底機能)で追い込んでから確定する。
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 9.0f, -43.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	camera_->Update();

	if (object3DManager_) {
		object3DManager_->SetDefaultCamera(camera_.get());
	}

	//===================================
	// ライト
	//===================================
	auto* lm = LightManager::GetInstance();
	lm->SetDirectionalLightDirection({ -0.4f, -1.0f, 0.3f });
	lm->SetDirectionalLightColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	lm->SetDirectionalLightIntensity(1.0f);

	//===================================
	// 背景(アリーナの一番奥)。カメラの正面方向へ一定距離だけ進めた位置に Plane を
	// 置いてテクスチャを貼る。サイズは distance でのカメラ視錐台の大きさに合わせた
	// 概算値(TitleScene::Initialize と同じ考え方。fovY≈0.45rad, 16:9 の計算値+余裕)。
	//===================================
	{
		constexpr float kBgDistance = 58.0f;
		const Vector3 camPos = camera_->GetTranslate();
		const Vector3 fwd = camera_->GetForward();
		const Vector3 bgPos{
			camPos.x + fwd.x * kBgDistance,
			camPos.y + fwd.y * kBgDistance,
			camPos.z + fwd.z * kBgDistance };

		background_ = std::make_unique<PrimitiveInstance>();
		background_->Initialize(PrimitiveInstance::PrimitiveType::Plane, "GameBackground");
		background_->SetCamera(camera_.get());
		background_->SetTranslate(bgPos);
		background_->SetRotate(camera_->GetRotate());
		background_->SetScale({ 52.0f, 29.0f, 1.0f });
		// アトラクト(デモ) = タイトル画面なので、旧 TitleScene と同じタイトル用背景を貼る。
		background_->SetTexture(attractMode_
			? "Resources/Textures/Title.dds"
			: "Resources/Textures/BackGround.dds");
	}

	//===================================
	// ステージ(CSV マップチップ)
	// 10=黒 / 20=白 の 1.0f 立方体で描画。1/2 のマスからスポーン座標を取り出す。
	// CSV が読めない場合は最下段だけ床にしたフォールバックで起動する。
	//===================================
	stageCatalog_.Scan();
	currentStageIndex_ = PickStartStageIndex();
	Log("GameScene: ステージ選出 -> " + stageCatalog_.NameAt(currentStageIndex_) + "\n");

	stage_ = std::make_unique<StageGrid>();
	stage_->LoadFromCsv(stageCatalog_.PathAt(currentStageIndex_));
	stage_->Initialize(camera_.get(), object3DManager_, dxCore_);

	//===================================
	// キャラクター
	// player_ は操作キャラ、enemy_ は敵キャラ(行動は enemyBrain_ が決める)。
	// どちらも Z=0 の同じ奥行きに置く(横視点なので全キャラ同じZ平面上にいる想定)。
	// スポーン座標と地形当たり判定はステージへ委譲する。
	//===================================
	playerSpawn_ = stage_->HasPlayerSpawn()
		? stage_->GetPlayerSpawnWorld()
		: Vector3{ -3.0f, 2.0f, 0.0f };
	const auto& enemySpawns = stage_->GetEnemySpawnsWorld();
	enemySpawn_ = !enemySpawns.empty() ? enemySpawns.front() : Vector3{ 3.0f, 2.0f, 0.0f };

	player_ = std::make_unique<Character>();
	player_->Initialize(camera_.get(), "Player", playerSpawn_);
	player_->SetStage(stage_.get());
	prevPlayerHP_ = player_->GetHP(); // 被弾振動の基準（初期化直後は満タン）
#ifdef USE_IMGUI
	player_->SetWeaponRenderContext(object3DManager_, dxCore_);
#endif // USE_IMGUI

	enemy_ = std::make_unique<Character>();
	enemy_->Initialize(camera_.get(), "Enemy", enemySpawn_);
        enemy_->SetStage(stage_.get());
#ifdef USE_IMGUI
	enemy_->SetWeaponRenderContext(object3DManager_, dxCore_);
#endif // USE_IMGUI

	// 見た目の仮 Box をスキニング付きアニメモデルに差し替える(アセットが無ければ Box のまま)。
	// プレイヤー=青 / 敵=赤。被弾中は赤・氷結中は水色に上書きされる。
	// アトラクト(デモ)は「敵 AI 同士の対戦」なので、両者とも敵の赤にする。
	// 色の定義元は MatchScoreText(得点表示も同じ色を使うため一本化している)。
	player_->SetupAnimatedModel(object3DManager_, skinningComputeManager_, dxCore_, srvManager_,
		attractMode_ ? MatchScoreText::kEnemyColor : MatchScoreText::kPlayerColor);
	enemy_->SetupAnimatedModel(object3DManager_, skinningComputeManager_, dxCore_, srvManager_,
		MatchScoreText::kEnemyColor);

	// 敵 AI と学習モデル。GameScene は Think() の結果を Character へ渡すだけ。
	enemyBrain_ = std::make_unique<EnemyBrain>();
	enemyBrain_->Initialize(kEnemyTurretMode);
	playerModel_ = std::make_unique<PlayerModel>();
	playerModel_->Reset();

	// アトラクト(デモ)モードでは、プレイヤー枠も AI が動かす(敵 AI 同士の対戦をループ再生)。
	if (attractMode_) {
		playerBrain_ = std::make_unique<EnemyBrain>();
		playerBrain_->Initialize(false);

		// タイトルロゴ(Title.mesh)を画面の少し上の方に置く。位置・スケールは仮値。
		if (object3DManager_ && dxCore_) {
			const Vector3 kTitleLogoPos{ 0.0f, 12.0f, -8.0f }; // x=中央 / y=中央より上 / z=手前(カメラ寄り)
			const Vector3 kTitleLogoScale{ 3.0f, 3.0f, 3.0f };
			titleLogo_ = std::make_unique<Object3DInstance>();
			titleLogo_->Initialize(object3DManager_, dxCore_, "Resources/Models/Title", "Title.mesh", "TitleLogo");
			titleLogo_->SetCamera(camera_.get());
			titleLogo_->SetScale(kTitleLogoScale);
			titleLogo_->SetTranslate(kTitleLogoPos);
			// OBJ 取り込み時の RH→LH 変換(x 反転)で文字が鏡像になるため、Y 軸 180° で戻す
			// (Block.obj と同じ対処。InstancedBlockRenderer の kUnbreakableRotation 参照)。
			titleLogo_->SetRotate({ 0.0f, kPi, 0.0f });
			titleLogo_->Update();
		}
	}

	// チュートリアル。説明の進行役と、攻撃してこない移動専用の敵 AI を用意する
	// (enemyBrain_ は生成だけされて使われない。切り分けは UpdateBattle 側で行う)。
	if (tutorialMode_) {
		tutorial_ = std::make_unique<TutorialDirector>();
		tutorial_->Reset();
		tutorialBrain_ = std::make_unique<TutorialEnemyBrain>();
		tutorialBrain_->Reset();
		tutorialRespawnTimer_ = 0.0f;
		tutorialFinished_ = false;
	}

	matchRule_.Reset();

	//===================================
	// デバッグ: 銃のパラメータをImGuiで調整できるようにする
	//===================================
#ifdef USE_IMGUI
	// 各 Weapon::DrawImGuiTuning() が触るのは(インスタンスではなく)クラス単位で共有する
	// static な値なので、ウィンドウの登録自体はプロセス中に1回で十分。GameScene::Initialize()
	// はタイトルへ戻って再度ゲームに入るたびに呼ばれる可能性があるため、static ローカル変数で
	// 二重登録(同じ名前のウィンドウが積み重なる)を防いでいる。
	static bool weaponTuningWindowRegistered = false;
	if (!weaponTuningWindowRegistered) {
		weaponTuningWindowRegistered = true;
		ImGuiManager::Instance().AddCallbackWindow("Weapon Tuning", []() {
			// 「どの武器をランダム抽選に混ぜるか」の一覧(Spawn Pool)と「各武器のパラメータ調整」
			// (Parameters)は別の関心事なので、CollapsingHeader を縦に並べるのではなく
			// ImGui::BeginTabBar でタブ分けする(スクロールが長くなりがちだったのを解消)。
			if (ImGui::BeginTabBar("WeaponTuningTabs")) {
				if (ImGui::BeginTabItem("Spawn Pool")) {
					// チェックを外すと CreateRandomWeapon の抽選候補から外れる(g_weaponSpawnPool[i].enabled
					// を直接触る。CreateRandomWeapon と共有するテーブル。既にステージに出ている物・
					// 拾得済みの物は消えない)。Spawn ボタンは enabled 状態に関係なく、その武器を
					// 1つだけ即座にステージへ湧かせる(デバッグ用。GameScene::SpawnSpecificWeaponPickup)。
					ImGui::TextUnformatted("Checkbox: ランダムスポーンの抽選に含める / Spawn: その場に1つ即時生成");
					ImGui::Separator();
					GameScene* self = GameScene::s_activeForDebug_;
					for (int i = 0; i < kWeaponSpawnPoolCount; ++i) {
						ImGui::PushID(i);
						ImGui::Checkbox(g_weaponSpawnPool[i].name, &g_weaponSpawnPool[i].enabled);
						ImGui::SameLine();
						if (ImGui::Button("Spawn") && self) {
							self->SpawnSpecificWeaponPickup(i);
						}
						ImGui::PopID();
					}
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Parameters")) {
					if (ImGui::CollapsingHeader("Pistol")) {
						ImGui::PushID("Pistol");
						Pistol::DrawImGuiTuning();
						ImGui::PopID();
					}
					if (ImGui::CollapsingHeader("AssaultRifle")) {
						ImGui::PushID("AssaultRifle");
						AssaultRifle::DrawImGuiTuning();
						ImGui::PopID();
					}
					if (ImGui::CollapsingHeader("Shotgun")) {
						ImGui::PushID("Shotgun");
						Shotgun::DrawImGuiTuning();
						ImGui::PopID();
					}
					if (ImGui::CollapsingHeader("Blaster")) {
						ImGui::PushID("Blaster");
						Blaster::DrawImGuiTuning();
						ImGui::PopID();
					}
					if (ImGui::CollapsingHeader("GrenadeLauncher")) {
						ImGui::PushID("GrenadeLauncher");
						GrenadeLauncher::DrawImGuiTuning();
						ImGui::PopID();
					}
					if (ImGui::CollapsingHeader("SniperRifle")) {
						ImGui::PushID("SniperRifle");
						SniperRifle::DrawImGuiTuning();
						ImGui::PopID();
					}
					if (ImGui::CollapsingHeader("Minigun")) {
						ImGui::PushID("Minigun");
						Minigun::DrawImGuiTuning();
						ImGui::PopID();
					}
					if (ImGui::CollapsingHeader("HandCannon")) {
						ImGui::PushID("HandCannon");
						HandCannon::DrawImGuiTuning();
						ImGui::PopID();
					}
					if (ImGui::CollapsingHeader("RicochetRifle")) {
						ImGui::PushID("RicochetRifle");
						RicochetRifle::DrawImGuiTuning();
						ImGui::PopID();
					}
					if (ImGui::CollapsingHeader("IceGun")) {
						ImGui::PushID("IceGun");
						IceGun::DrawImGuiTuning();
						ImGui::PopID();
					}
					if (ImGui::CollapsingHeader("FireGun")) {
						ImGui::PushID("FireGun");
						FireGun::DrawImGuiTuning();
						ImGui::PopID();
					}
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		});
	}

	// デバッグ: Resources/Stages/ のステージ一覧を出し、クリックで切り替える。
	// 切り替えると LoadStage がプレイヤー・敵をそのステージの初期位置へ戻す
	// (本番のステージ遷移でもそのまま使える形)。
	static bool stageSelectWindowRegistered = false;
	if (!stageSelectWindowRegistered) {
		stageSelectWindowRegistered = true;
		ImGuiManager::Instance().AddCallbackWindow("Stage Select", []() {
			GameScene* self = GameScene::s_activeForDebug_;
			if (!self) {
				ImGui::TextUnformatted("(GameScene inactive)");
				return;
			}
			ImGui::Text("Current: %s", self->stageCatalog_.NameAt(self->currentStageIndex_).c_str());
			ImGui::Separator();
			for (int i = 0; i < self->stageCatalog_.Count(); ++i) {
				const bool selected = (i == self->currentStageIndex_);
				if (ImGui::Selectable(self->stageCatalog_.NameAt(i).c_str(), selected)) {
					self->pendingStageLoad_ = i; // 実際の読み込みは次の Update 先頭で行う
				}
			}
			ImGui::Separator();
			if (ImGui::Button("Reload / Reset Positions")) {
				self->pendingStageLoad_ = self->currentStageIndex_;
			}
		});
	}

	// デバッグ: セーブデータ(チュートリアル完了フラグ)の確認とリセット。
	// タイトルの SPACE が「チュートリアルへ」「本編へ」のどちらに飛ぶかをここで切り替えられる。
	static bool saveDataWindowRegistered = false;
	if (!saveDataWindowRegistered) {
		saveDataWindowRegistered = true;
		ImGuiManager::Instance().AddCallbackWindow("Save Data", []() {
			ImGui::Text("File: %s", SaveData::GetFilePath());
			ImGui::Text("Tutorial cleared: %s", SaveData::IsTutorialCleared() ? "yes" : "no");
			ImGui::Separator();
			if (ImGui::Button("Clear tutorial flag (start from tutorial)")) {
				SaveData::SetTutorialCleared(false);
			}
			if (ImGui::Button("Mark tutorial as cleared (skip tutorial)")) {
				SaveData::SetTutorialCleared(true);
			}
		});
	}
#endif

	//===================================
	// サウンド。LoadFile はファイル全体を読み込むため、プロセス中に1回だけ行えばよい
	// (Weapon Tuning ウィンドウの登録と同じ static ローカル変数によるガード)。
	//===================================
	{
		static bool soundsLoaded = false;
		if (!soundsLoaded) {
			soundsLoaded = true;
			auto* sm = SoundManager::GetInstance();
			sm->LoadFile("GameBGM", "Resources/Sounds/Game/GameBGM.mp3");
			sm->LoadFile("TitleBGM", "Resources/Sounds/Title/TitleBGM.mp3"); // アトラクト(旧 TitleScene)用 BGM
			sm->LoadFile("PinchBGM", "Resources/Sounds/Pinch/Pinch_Alarm.mp3"); // 相手が王手(あと1点で勝利)のとき GameBGM から差し替える
			// 素手(パンチのバリエーション。UnarmedWeapon.cpp の kPunchSoundNames と対応させる)
			sm->LoadFile("Punch_Big", "Resources/Sounds/SE/BareHands/Punch_Big.mp3");
			sm->LoadFile("Punch_Heavy1", "Resources/Sounds/SE/BareHands/Punch_Heavy1.mp3");
			sm->LoadFile("Punch_Light1", "Resources/Sounds/SE/BareHands/Punch_Light1.mp3");
			sm->LoadFile("Punch_Light2", "Resources/Sounds/SE/BareHands/Punch_Light2.mp3");
			// 銃全般で共有する効果音(武器ごとではなく1つを使い回す)
			sm->LoadFile("Empty", "Resources/Sounds/SE/Empty.mp3"); // 空撃ちクリック(全銃共通)
			sm->LoadFile("Drop", "Resources/Sounds/SE/Drop.mp3");   // 弾が残った状態で投げた銃が着地した音(全銃共通)
			// 銃器の発射音・付随音
			sm->LoadFile("Pistol_Fire", "Resources/Sounds/SE/Pistol/Pistol_Fire.mp3");
			sm->LoadFile("AssaultRifle_Fire", "Resources/Sounds/SE/Assault/AssaultRifle_Fire.mp3");
			sm->LoadFile("Shotgun_Fire", "Resources/Sounds/SE/Shotgun/Shotgun_Fire.mp3");
			sm->LoadFile("Shotgun_Pump", "Resources/Sounds/SE/Shotgun/Shotgun_Pump.mp3");
			sm->LoadFile("Blaster_Fire", "Resources/Sounds/SE/Blaster/Blaster_Fire.mp3");
			sm->LoadFile("Blaster_Explosion", "Resources/Sounds/SE/Blaster/Blaster_Explosion.mp3");
			sm->LoadFile("GrenadeLauncher_Fire", "Resources/Sounds/SE/GrenadeLauncher/GrenadeLauncher.mp3");
			sm->LoadFile("GrenadeLauncher_Bounce", "Resources/Sounds/SE/GrenadeLauncher/GrenadeLauncher_Bounce.mp3");
			sm->LoadFile("Explosion_Default", "Resources/Sounds/SE/GrenadeLauncher/Explosion_Default.mp3");
			sm->LoadFile("SniperRifle_Fire", "Resources/Sounds/SE/Sniper/SniperRifle_Fire.mp3");
			sm->LoadFile("SniperRifle_Bolt", "Resources/Sounds/SE/Sniper/SniperRifle_Bolt.mp3");
			sm->LoadFile("Minigun_Fire", "Resources/Sounds/SE/Minigun/Minigun_Fire.mp3");
			sm->LoadFile("HandCannon_Fire", "Resources/Sounds/SE/HandCannon/HandCannon_Fire.mp3");
			sm->LoadFile("RicochetRifle_Fire", "Resources/Sounds/SE/Ricochet/RicochetRifle_Fire.mp3");
			sm->LoadFile("RicochetRifle_Bounce", "Resources/Sounds/SE/Ricochet/RicochetRifle_Bounce.mp3");
			sm->LoadFile("IceGun_Fire", "Resources/Sounds/SE/Freeze/IceGun_Fire.mp3");
			sm->LoadFile("FireGun_Fire", "Resources/Sounds/SE/Flamethrower/FireGun_Fire.mp3");
			sm->LoadFile("FireHazard_Ignite", "Resources/Sounds/SE/Fire/FireHazard_Ignite.mp3");
		}
		// アトラクト(タイトル)は旧 TitleScene::Initialize と同じ TitleBGM を鳴らす。
		// BGM はループ再生(通常の Play2DSound は曲が終わると止まる)。
		bgmPinch_ = false;
		SoundManager::GetInstance()->Play2DSoundLooped(attractMode_ ? "TitleBGM" : "GameBGM");
	}

	// 状態異常アウトライン(炎=赤/氷=青の点滅)の ID パスを GameApp に配線する。
	// GameApp が PostEffect の IdPass 内でこれを呼び、炎/氷のキャラのシルエットを idMaskRT へ描く。
	GameApp::SetStatusOutlineDrawer([this](ID3D12GraphicsCommandList* /*cmd*/) -> bool {
		bool drew = false;
		if (player_ && player_->GetStatusOutlineId() != 0) { player_->DrawStatusOutlineIdPass(dxCore_); drew = true; }
		if (enemy_ && enemy_->GetStatusOutlineId() != 0) { enemy_->DrawStatusOutlineIdPass(dxCore_); drew = true; }
		return drew;
	});

	RefreshPortalEffects(); // 各ポータル位置に Warp エフェクトを常駐再生

	// 最初のラウンドも、得点によるステージ切替と同じく3秒カウントダウンを挟んでから始める。
	StartRoundCountdown();
}

void GameScene::Finalize() {
	if (s_activeForDebug_ == this) {
		s_activeForDebug_ = nullptr;
	}
	GameApp::SetStatusOutlineDrawer(nullptr); // 状態異常アウトラインの配線を解除
	SoundManager::GetInstance()->Stop2DSound("TitleBGM");
	SoundManager::GetInstance()->Stop2DSound("GameBGM");
	SoundManager::GetInstance()->Stop2DSound("PinchBGM");
	StopRumble(); // シーンを抜けるときにコントローラー振動を鳴らしっぱなしにしない
	for (EffectHandle h : portalEffectHandles_) {
	EffectManager::GetInstance()->Stop(h);
}
	portalEffectHandles_.clear();
	// 依存関係はないが、生成順と逆順に破棄する(可読性のための慣習)。
	pickups_.clear();
	flyingObjects_.clear();
	fireHazards_.clear();
	titleLogo_.reset();
	tutorial_.reset();
	tutorialBrain_.reset();
	playerModel_.reset();
	playerBrain_.reset();
	enemyBrain_.reset();
	enemy_.reset();
	player_.reset();
	stage_.reset();
	background_.reset();
	camera_.reset();
}

void GameScene::LoadStage(int index) {
	if (stageCatalog_.Empty()) {
		return;
	}
	if (index < 0 || index >= stageCatalog_.Count()) {
		index = 0;
	}
	currentStageIndex_ = index;
	Log("GameScene: ステージ切り替え -> " + stageCatalog_.NameAt(index) + "\n");

	// ステージを丸ごと作り直す（ギミックの状態・トゲモデルもここで一新される）。
	stage_->Finalize();
	stage_->LoadFromCsv(stageCatalog_.PathAt(index));
	stage_->Initialize(camera_.get(), object3DManager_, dxCore_);

	// 新しいステージのマップチップ初期位置を取り出す（Initialize と同じ既定値フォールバック）。
	playerSpawn_ = stage_->HasPlayerSpawn()
		? stage_->GetPlayerSpawnWorld()
		: Vector3{ -3.0f, 2.0f, 0.0f };
	const auto& enemySpawns = stage_->GetEnemySpawnsWorld();
	enemySpawn_ = !enemySpawns.empty() ? enemySpawns.front() : Vector3{ 3.0f, 2.0f, 0.0f };

	// 安全機能：ステージ切り替えでコントローラー振動を止める（切替直前の被弾/爆発の振動を持ち越さない）。
	StopRumble();

	// プレイヤー・敵を初期位置へ戻す（HP・速度・状態異常もクリアされる）。
	if (player_) player_->ResetForNewRound(playerSpawn_);
	if (player_) prevPlayerHP_ = player_->GetHP(); // 被弾振動の基準を新ステージの満タン HP に合わせる
	if (enemy_)  enemy_->ResetForNewRound(enemySpawn_);
	if (enemyBrain_) enemyBrain_->ResetForNewRound();
	if (playerBrain_) playerBrain_->ResetForNewRound();

	// ステージに散らばっていた弾・武器・炎・デバッグ表示は持ち越さない。
	flyingObjects_.clear();
	pickups_.clear();
	fireHazards_.clear();
	debugFlashes_.clear();
	// ラウンド開始直後は短い方の間隔(kInitialWeaponSpawnDelay)で最初の1丁を湧かせる。
	weaponSpawnTimer_ = kInitialWeaponSpawnDelay;

	// 爆発・被弾などの再生中エフェクトも旧ステージの位置に残ったままにしない。
	// EffectManager はアプリ全体で1つのシングルトン(GameApp::Initialize で Initialize/Finalize)
	// なので、ここで明示的に StopAll() しないと次のステージへそのまま持ち越されてしまう。
	EffectManager::GetInstance()->StopAll();

	playerInPortal_ = false;
	enemyInPortal_ = false;

	RefreshPortalEffects();

	// 新しいステージでの戦闘は、旧ステージでの決着直後にいきなり始めない。3秒待たせる。
	StartRoundCountdown();
}

void GameScene::RefreshPortalEffects() {
	// 前のステージぶんの Warp エフェクトを止める。
	for (EffectHandle h : portalEffectHandles_) {
		EffectManager::GetInstance()->Stop(h);
	}
	portalEffectHandles_.clear();

	if (!stage_) {
		return;
	}
	// 各ポータル位置に loop の Warp エフェクトを常駐再生する。
	for (const Vector3& p : stage_->GetPortalWorldPositions()) {
		const EffectHandle h = EffectManager::GetInstance()->Play("Warp", p);
		if (h != kInvalidEffectHandle) {
			portalEffectHandles_.push_back(h);
		}
	}
}

void GameScene::StartRoundCountdown() {
	// アトラクト(デモ)とチュートリアルはカウントダウン無しで即開始する
	// (どちらもラウンド制ではないので「次のラウンドまで3秒」に意味が無い)。
	if (attractMode_ || tutorialMode_) {
		roundState_ = RoundState::Battle;
		countdownRemaining_ = 0.0f;
		return;
	}
	roundState_ = RoundState::Countdown;
	countdownRemaining_ = kRoundCountdownSeconds;
}

void GameScene::UpdateRoundCountdown(float dt) {
	// 操作・AI思考・攻撃・得点判定は止めるが、重力・接地・アイドル姿勢だけは効かせておく
	// (カウントダウン明けにいきなり宙に浮いた状態から始まらないようにするため)。
	const CharacterInput neutral{};
	if (player_) {
		player_->Update(dt, neutral.moveX, neutral.jumpTriggered, neutral.crouchHeld,
			neutral.aimDirX, neutral.aimDirY, neutral.attackTriggered, neutral.attackHeld, neutral.throwTriggered);
	}
	if (enemy_) {
		enemy_->Update(dt, neutral.moveX, neutral.jumpTriggered, neutral.crouchHeld,
			neutral.aimDirX, neutral.aimDirY, neutral.attackTriggered, neutral.attackHeld, neutral.throwTriggered);
	}
	CollisionSystem::GetInstance()->Update();

	// UpdateBattle 以外の状態でこれを呼ばないと、EffectManager/GPUParticleManager の
	// シミュレーション(Update)が丸ごと止まる。StopAll() で止めたはずのエフェクトも、
	// 実際に GPU 側のパーティクルバッファへ反映されるのは次の Update 呼び出しなので、
	// これが無いと「消したはずのエフェクトが直前のフレームの見た目のまま静止して残る」
	// (カウントダウン中ずっと固まって見える)原因になる。
	UpdateGlobalEffects(camera_.get(), dxCore_ ? dxCore_->GetDeltaTime() : dt);

	countdownRemaining_ -= dt;
	if (countdownRemaining_ <= 0.0f) {
		roundState_ = RoundState::Battle;
	}
}

void GameScene::UpdateRoundEnd(float dt) {
	// カウントダウン中と同じく、操作/AI/攻撃/得点判定は止めるが重力・接地・アイドル姿勢だけ効かせる。
	const CharacterInput neutral{};
	if (player_) {
		player_->Update(dt, neutral.moveX, neutral.jumpTriggered, neutral.crouchHeld,
			neutral.aimDirX, neutral.aimDirY, neutral.attackTriggered, neutral.attackHeld, neutral.throwTriggered);
	}
	if (enemy_) {
		enemy_->Update(dt, neutral.moveX, neutral.jumpTriggered, neutral.crouchHeld,
			neutral.aimDirX, neutral.aimDirY, neutral.attackTriggered, neutral.attackHeld, neutral.throwTriggered);
	}
	CollisionSystem::GetInstance()->Update();

	// UpdateRoundCountdown と同じ理由(コメント参照)で、決着直後の猶予中もエフェクトの
	// シミュレーションだけは進めておく。撃破エフェクトそのものをここで見せ切るためにも必要。
	UpdateGlobalEffects(camera_.get(), dxCore_ ? dxCore_->GetDeltaTime() : dt);

	roundEndRemaining_ -= dt;
	if (roundEndRemaining_ > 0.0f || roundEndActionTaken_) {
		return;
	}
	// 猶予明け。ここから先は一度きり(roundEndActionTaken_ で多重実行を防ぐ。
	// 猶予0秒後もこの関数は数フレーム呼ばれ続けるため、例えば Result への ChangeScene を
	// 毎フレーム呼び直してフェードが終わらなくなる、といった事故を防ぐ)。
	roundEndActionTaken_ = true;

	if (roundEndMatchOver_ && !attractMode_) {
		// このセットは決着。結果を relay に残して Result シーンへ(ステージ切替はしない)。
		// LoadStage を通らない経路なので、ここでも明示的に StopAll() しないと最後の一撃の
		// 撃破エフェクトが Result シーンまで残ったまま持ち越されてしまう。
		EffectManager::GetInstance()->StopAll();
		// 安全機能：ゲーム終了（Result へ遷移）でコントローラー振動を止める。
		StopRumble();
		// このセットは決着。次セットのステージ抽選が新しい一巡から始まるようバッグを空にする。
		stageCatalog_.ResetBattleRotation();
		MatchResultRelay::SetResult(matchRule_.GetWinner(), matchRule_.GetPlayerPoints(), matchRule_.GetEnemyPoints());
		SceneManager::GetInstance()->ChangeScene("Result", TransitionType::Fade);
	} else {
		// アトラクト時はセットが決着しても Result へ行かず、得点を 0 に戻して次のセットを続ける
		// (タイトル画面の裏で永久にデモが回り続ける)。決着していない場合は通常どおり次ラウンドへ。
		if (roundEndMatchOver_) {
			EffectManager::GetInstance()->StopAll();
			matchRule_.Reset();
			// アトラクトも次セットは新しい一巡から。
			stageCatalog_.ResetBattleRotation();
		}
		// 次のラウンドのステージ。アトラクト時は Sample 固定、通常時はランダム抽選
		// (実際の読み込みと地形の作り直しは次フレーム先頭の pendingStageLoad_ 解決で行う。
		//  LoadStage が地形・スポーン位置・カウントダウンの再開始までまとめて面倒を見る)。
		pendingStageLoad_ = PickStartStageIndex();
	}
}

float GameScene::BeltShiftX(const Vector3& center, const Vector3& half, float dt) const {
	// マージンぶん内側で見て、しっかり乗っていれば通常速度で搬送。
	const int solidDir = stage_->BeltDirUnderAabb(center, half, kBeltEdgeMargin);
	if (solidDir != 0) {
		return static_cast<float>(solidDir) * kBeltSpeed * dt;
	}

	// 端。左足／右足の直下を個別に見て、どちらの端からはみ出しているかで挙動を変える。
	const float footY = center.y - half.y - 0.05f;
	const int dirL = stage_->BeltDirAtPoint(center.x - half.x + 0.02f, footY);
	const int dirR = stage_->BeltDirAtPoint(center.x + half.x - 0.02f, footY);
	const int touch = (dirL != 0) ? dirL : dirR;  // 搬送方向（乗っている足が示す向き）
	if (touch == 0) {
		return 0.0f;  // どちらの足もベルト外
	}

	// touch>0（右搬送）: 上流側=左足(-X) / 下流側=右足(+X)。touch<0 で左右逆。
	const bool upstreamSideFootOn   = (touch > 0) ? (dirL != 0) : (dirR != 0);
	const bool downstreamSideFootOn = (touch > 0) ? (dirR != 0) : (dirL != 0);

	if (upstreamSideFootOn && !downstreamSideFootOn) {
		// 上流側の足だけ乗っている＝下流端からはみ出している
		// → バランスを取らせず強めに押し出して落とす（従来の挙動）。
		return static_cast<float>(touch) * kBeltSpeed * kBeltEdgeEjectMul * dt;
	}
	if (downstreamSideFootOn && !upstreamSideFootOn) {
		// 下流側の足だけ乗っている＝上流端からはみ出している
		// → もう掴まない（自分の移動＋重力で外れる。ここが今回の修正点）。
		return 0.0f;
	}
	// 両足乗っている（端付近だが乗ってはいる）→ 通常搬送。
	return static_cast<float>(touch) * kBeltSpeed * dt;
}

void GameScene::UpdateStageGimmicks(float dt) {
	if (!stage_) {
		return;
	}

	// 敵がこのフレームにトゲで即死したか（AI の「危険地形への慎重さ」学習に渡す）。
	// KO 判定(CheckKnockoutAndReset の後)で消費する。
	enemyDeathBySpike_ = false;

	auto applyBelt = [&](Character& c) {
		if (!c.IsGrounded()) {
			return;
		}
		const float sx = BeltShiftX(c.GetColliderCenter(), c.GetColliderHalfExtent(), dt);
		if (sx != 0.0f) {
			const Vector3 p = c.GetPosition();
			c.SetPosition({ p.x + sx, p.y, p.z });
		}
	};

	auto applySpike = [&](Character& c, bool* spikeDeathOut) {
		// 当たり判定 AABB 全体で重なりを見る（しゃがみ中は高さが縮む）。
		if (!c.IsDead()
			&& stage_->OverlapsSpike(c.GetColliderCenter(), c.GetColliderHalfExtent())) {
			c.ApplyDamage(100000.0f); // 即死。CheckKnockoutAndReset がリスポーンを処理する
			if (spikeDeathOut) {
				*spikeDeathOut = true;
			}
		}
	};

	auto applyPortal = [&](Character& c, bool& portalLocked) {
		const Vector3 cc = c.GetColliderCenter();
		const Vector3 ch = c.GetColliderHalfExtent();

		// ワープに一切重なっていない → ロック解除（次のワープに入れる）。
		if (!stage_->OverlapsAnyPortal(cc, ch)) {
			portalLocked = false;
			return;
		}
		// 出てきたワープにまだ体が重なっている間は再ワープさせない。
		// ジャンプ／しゃがみ／左右移動で完全に離れて初めて上の分岐で解除される。
		if (portalLocked) {
			return;
		}
		Vector3 dest{};
		if (stage_->TryPortal(cc, ch, dest)) {
			c.SetPosition(dest);
			c.CancelMomentum(); // 慣性で出口ブロックから流れ落ちないように
			portalLocked = true;
		}
	};

	applyBelt(*player_);
	applyBelt(*enemy_);
	applySpike(*player_, nullptr);
	applySpike(*enemy_, &enemyDeathBySpike_);
	applyPortal(*player_, playerInPortal_);
	applyPortal(*enemy_, enemyInPortal_);

	// 起爆した爆弾ブロックの爆風をキャラへ適用する（地形削り・誘爆は StageGrid 内で完結済み）。
	auto applyBlast = [](Character& c, const StageGrid::BombExplosion& ex) {
		const Vector3 p = c.GetPosition();
		const float dx = p.x - ex.center.x;
		const float dy = p.y - ex.center.y;
		const float dist = std::sqrt(dx * dx + dy * dy);
		if (dist >= ex.radius) {
			return;
		}
		const float falloff = 1.0f - dist / ex.radius; // 爆心=1.0 → 端=0.0
		c.ApplyDamage(ex.damage * falloff);
		// 外向き＋やや上向きに吹き飛ばす（放射方向。角度はリアル寄りに +0.35 の上バイアス）。
		c.ApplyBlastKnockback(dx, dy + 0.35f, kBombKnockbackPower * falloff);
	};
	for (const StageGrid::BombExplosion& ex : stage_->ConsumeBombExplosions()) {
		AddDebugFlash(ex.center, ex.radius, Vector4{ 1.0f, 0.4f, 0.05f, 1.0f }, 0.5f);
		EffectManager::GetInstance()->Play("Block_Exprosion", ex.center);
		TriggerExplosionRumble(ex.center);
		Log("爆弾ブロックが起爆\n");
		applyBlast(*player_, ex);
		applyBlast(*enemy_, ex);
	}
}

void GameScene::Update() {
	// ステージ切り替え要求は Update の先頭でだけ実行する。
	// ImGui コールバック（EndFrame 中＝コマンドリスト記録後）から直接 stage_ を作り直すと、
	// まだ実行中のコマンドリストが参照しているリソースを解放してしまい D3D12 #921 になる。
	if (pendingStageLoad_ >= 0) {
		const int idx = pendingStageLoad_;
		pendingStageLoad_ = -1;
		LoadStage(idx);
	}

	// デバッグカメラが有効ならそちらの行列をシーンカメラへ注入する(Scene基底の機能)。
	// 無効なら通常どおり自前のカメラを更新する。
	UpdateDebugCameraIfActive();
	if (!GetUseDebugCamera()) {
		camera_->Update();
	}

	if (background_) {
		background_->Update();
	}

	// 3D音の定位をカメラへ追従させつつ、再生終了検知を毎フレーム進める(08_Audio.md、
	// 呼び忘れると3D音の定位が固まり、再生終了の検知も走らない)。
	SoundManager::GetInstance()->UpdateListener(camera_.get());
	SoundManager::GetInstance()->Update();

	// 本編で相手が王手(あと1点で勝利 = 9点)になったら BGM を GameBGM → PinchBGM に差し替える
	// (bgmPinch_ で1回だけ切り替える。アトラクト/チュートリアルは対象外)。
	if (!attractMode_ && !tutorialMode_) {
		const bool pinch = matchRule_.GetEnemyPoints() >= MatchRule::kPointsToWin - 1;
		if (pinch != bgmPinch_) {
			bgmPinch_ = pinch;
			SoundManager::GetInstance()->Play2DSoundLooped(pinch ? "PinchBGM" : "GameBGM");
		}
	}

	if (titleLogo_) {
		titleLogo_->Update();

		// タイトルロゴの UV を横へ流し続ける(虹テクスチャがスクロールする)。
		// PS 側が mul(float4(texcoord,0,1), uvTransform) するので、平行移動行にオフセットを入れる。
		constexpr float kTitleLogoUvScrollSpeed = 0.15f; // 1秒あたりのテクスチャ周回数
		titleLogoUvScroll_ += GetScaledDeltaTime(TimeGroup::UI) * kTitleLogoUvScrollSpeed;
		titleLogoUvScroll_ -= std::floor(titleLogoUvScroll_); // 0..1 に丸めてループ
		if (auto* mi = titleLogo_->GetModelInstance()) {
			Matrix4x4 uv = MakeIdentity4x4();
			uv.m[3][0] = titleLogoUvScroll_; // U 方向へ平行移動
			for (const auto& sm : mi->GetSubmeshes()) {
				if (sm.material) {
					sm.material->uvTransform = uv;
				}
			}
		}
	}

	// ゲームロジックは Player グループの時間で進める。
	// ヒットストップやスローを入れるときにここが効く
	const float dt = GetScaledDeltaTime(TimeGroup::Player);

	// コントローラー振動の減衰。ヒットストップ中に振動が固まらないよう unscaled な実 delta で進める。
	UpdateRumble(dxCore_ ? dxCore_->GetDeltaTime() : dt);

	//===================================
	// 入力 → プレイヤーの意図(左右移動・ジャンプ・しゃがみ・攻撃・照準・投げ捨て)への変換
	//
	// ここが「入力デバイス」と「Character の中身」を繋ぐ唯一の場所。
	// Character::Update() はキーボードもゲームパッドも一切知らないので、
	// GameScene が代わりにデバイスの生の状態を読み、意味のある意図(moveX 等)に
	// 変換してから渡している。AI(フェーズ4)を実装するときは、ここでの
	// 「キーボード/パッドを読む」処理の代わりに「AIが行動を決める」処理を書き、
	// 同じ Character::Update() を呼べばよい(Character 側は無改造で済む)。
	//
	// 横視点ゲームなので移動はX軸のみ(左スティックの上下=奥行き成分は使わない)。
	// 照準はマウスカーソル方向(キーボード操作時)/右スティック方向(ゲームパッド接続時、
	// 実際に倒されているときだけ優先)で、移動方向とは完全に独立している。
	//===================================
	float moveX = 0.0f;
	bool jumpTriggered = false;
	bool crouchHeld = false;
	bool attackTriggered = false;
	bool attackHeld = false;
	bool throwTriggered = false;
	LONG mouseClientX = 0;
	LONG mouseClientY = 0;
	if (input_) {
		if (auto* mouseForAim = input_->GetMouse()) {
			mouseClientX = mouseForAim->GetClientX();
			mouseClientY = mouseForAim->GetClientY();
		}
	}
	const Vector2 mouseNdc = ComputeMouseNdc(mouseClientX, mouseClientY);
	Vector2 aimDir = ComputeAimDirectionFromNdc(*camera_, mouseNdc.x, mouseNdc.y, player_->GetPosition(), &lastAimWorldPoint_);
	if (input_) {
		if (auto* kb = input_->GetKeyboard()) {
			if (kb->PushKey(DIK_A)) moveX -= 1.0f;
			if (kb->PushKey(DIK_D)) moveX += 1.0f;
			jumpTriggered |= kb->TriggerKey(DIK_W);  // 押した瞬間だけ true(押しっぱなしで連続ジャンプしない)
			crouchHeld |= kb->PushKey(DIK_S);        // 押している間ずっと true
			throwTriggered |= kb->TriggerKey(DIK_R); // 武器投げ捨て
		}
		if (auto* mouse = input_->GetMouse()) {
			// 左クリックが攻撃(素手パンチ/銃の発射どちらもこの1つの入力で兼用。
			// どちらが起きるかは Character 側が装備中の Weapon に委譲して決める)。
			attackTriggered |= mouse->IsButtonTriggered(MouseInput::Button::Left);
			attackHeld |= mouse->IsButtonPressed(MouseInput::Button::Left);
			throwTriggered |= mouse->IsButtonTriggered(MouseInput::Button::Right); // 右クリックで投げ捨て
		}
		if (auto* pad = input_->GetController()) {
			auto ls = pad->GetLeftStick();
			if (ls.magnitude > 0.0f) {
				// magnitude(倒し具合)を掛けることで、軽く倒したときはゆっくり動く自然な挙動にする
				moveX += ls.x * ls.magnitude;
			}
			jumpTriggered |= pad->IsButtonTriggered(XINPUT_GAMEPAD_A);
			// しゃがみは「左スティックを下に倒す」か「Dパッド下」のどちらでも入る
			crouchHeld |= (ls.y < -0.5f) || pad->IsButtonPressed(XINPUT_GAMEPAD_DPAD_DOWN);
			attackTriggered |= pad->IsButtonTriggered(XINPUT_GAMEPAD_X);
			attackHeld |= pad->IsButtonPressed(XINPUT_GAMEPAD_X);
			throwTriggered |= pad->IsButtonTriggered(XINPUT_GAMEPAD_Y);

			// 右スティックが実際に倒されているときだけ、マウス照準より優先する
			// (倒されていない間、マウスカーソルの位置がそのまま照準として使われ続ける)。
			auto rs = pad->GetRightStick();
			if (rs.magnitude > 0.2f) {
				aimDir = { rs.x, rs.y };
			}
		}
	}

	stage_->Update(dt);

	// デバイスから読んだ生の状態を、解決済みの意図(CharacterInput)にまとめる。
	// プレイヤーも敵 AI も、ここから先は同じ CharacterInput 経由で Character を動かす。
	CharacterInput playerInput;
	playerInput.moveX = moveX;
	playerInput.jumpTriggered = jumpTriggered;
	playerInput.crouchHeld = crouchHeld;
	playerInput.aimDirX = aimDir.x;
	playerInput.aimDirY = aimDir.y;
	playerInput.attackTriggered = attackTriggered;
	playerInput.attackHeld = attackHeld;
	playerInput.throwTriggered = throwTriggered;

	// 決着直後の猶予(RoundEnd)・ラウンド開始前の3秒カウントダウン(Countdown)中は、
	// ゲーム進行(入力・AI・攻撃・得点判定)を止める。
	if (roundState_ == RoundState::Battle) {
		UpdateBattle(dt, playerInput);
	} else if (roundState_ == RoundState::RoundEnd) {
		UpdateRoundEnd(dt);
	} else {
		UpdateRoundCountdown(dt);
	}

	//===================================
	// デバッグ表示の残り時間を進める(実際の描画は Draw() 側)
	//===================================
	UpdateDebugFlashes(dt);

	//===================================
	// シーン遷移。
	//   アトラクト(デモ)モード : SPACE/Enter/(A) で本編(Game)へ。
	//   通常                    : ESC/(B) でタイトルへ戻る。
	//===================================
	if (attractMode_) {
		bool start = false;
		if (input_) {
			if (auto* kb = input_->GetKeyboard()) {
				start |= kb->TriggerKey(DIK_SPACE) || kb->TriggerKey(DIK_RETURN);
			}
			if (auto* pad = input_->GetController()) {
				start |= pad->IsButtonTriggered(XINPUT_GAMEPAD_A);
			}
		}
		if (start) {
			// チュートリアル未完了なら、本編の前にチュートリアルへ寄り道させる
			// (完了フラグは SaveData に永続化されるので、2回目以降は直接本編へ入る)。
			const char* next = SaveData::IsTutorialCleared() ? "Game" : "Tutorial";
			SceneManager::GetInstance()->ChangeScene(next, TransitionType::Fade);
		}
	} else {
		bool back = false;
		if (input_) {
			if (auto* kb = input_->GetKeyboard()) {
				back |= kb->TriggerKey(DIK_ESCAPE);
			}
			// チュートリアル中の (B) は「説明を次へ」なので、タイトル復帰には使わない。
			if (auto* pad = input_->GetController(); pad && !tutorialMode_) {
				back |= pad->IsButtonTriggered(XINPUT_GAMEPAD_B);
			}
		}
		if (back) {
			SceneManager::GetInstance()->ChangeScene("Title", TransitionType::Fade);
		}
	}
}

CharacterInput GameScene::DecideAiInput(EnemyBrain& brain, Character& self, Character& target,
	const PlayerModel* model, float dt) {
	// self が素手のとき拾いに行けるよう、取得可能で最寄りの武器 pickup を渡す。
	// 「真上の別プラットフォームにあって歩いても跳んでも届かない」もの、
	// および AI が「届かない」と判断して避けているものは候補から除く。
	Vector3 nearestPickupPos{};
	bool hasNearestPickup = false;
	{
		const Vector3 ep = self.GetPosition();
		const EnemyBrain::PickupAvoid avoid = brain.GetPickupAvoid();
		float best = 1e18f;
		for (const auto& pk : pickups_) {
			if (pk->IsTaken()) continue;
			const Vector3 pp = pk->GetPosition();
			const float ddx = pp.x - ep.x;
			const float ddy = pp.y - ep.y;
			if (ddy > 3.0f && std::fabs(ddx) < 1.5f) continue;               // 真上で届かない
			if (avoid.active && std::fabs(pp.x - avoid.x) < 2.5f) continue;  // AI が諦めた場所
			const float d2 = ddx * ddx + ddy * ddy;
			if (d2 < best) { best = d2; nearestPickupPos = pp; hasNearestPickup = true; }
		}
	}

	// self に向かって飛んでくる弾（相手が撃ったもの）を探す。回避判断に使う。
	Vector3 threatPos{};
	Vector3 threatVel{};
	float threatTtc = 0.0f;
	bool threatActive = false;
	{
		const Vector3 ep = self.GetPosition();
		float bestTtc = 1e9f;
		for (const auto& obj : flyingObjects_) {
			if (obj->IsDead() || obj->GetOwner() == &self) {
				continue; // 自分の弾は脅威じゃない
			}
			const Vector3 pp = obj->GetPosition();
			const Vector3 pv = obj->GetVelocity();
			const float dx = ep.x - pp.x;
			if (dx * pv.x <= 0.0f || std::fabs(pv.x) < 1.0f) {
				continue; // self の方へ向かっていない
			}
			const float ttc = dx / pv.x;
			// 到達時点の弾の高さが self の胴体あたりを通るか（ざっくり）。
			const float yAtHit = pp.y + pv.y * ttc;
			if (std::fabs(yAtHit - ep.y) > 1.6f) {
				continue;
			}
			if (ttc < bestTtc) {
				bestTtc = ttc;
				threatPos = pp;
				threatVel = pv;
				threatTtc = ttc;
				threatActive = true;
			}
		}
	}

	BrainContext brainCtx;
	brainCtx.self = &self;
	brainCtx.target = &target;
	brainCtx.stage = stage_.get();
	brainCtx.playerModel = model;
	brainCtx.nearestPickup = hasNearestPickup ? &nearestPickupPos : nullptr;
	brainCtx.incomingThreat = threatActive;
	brainCtx.threatPos = threatPos;
	brainCtx.threatVel = threatVel;
	brainCtx.threatTtc = threatTtc;
	brainCtx.dt = dt;
	return brain.Think(brainCtx);
}

void GameScene::UpdateBattle(float dt, const CharacterInput& playerInput) {
	// 敵の意図は EnemyBrain が決める(入力デバイスは一切読まない)。学習モデルは playerModel_。
	// チュートリアル中だけは、攻撃してこない移動専用の TutorialEnemyBrain に差し替える。
	CharacterInput enemyInput;
	if (tutorialMode_ && tutorialBrain_) {
		enemyInput = tutorialBrain_->Think(*enemy_, *player_, stage_.get(), dt);
	} else {
		enemyInput = DecideAiInput(*enemyBrain_, *enemy_, *player_, playerModel_.get(), dt);
	}

	// プレイヤー枠の意図。通常は引数の playerInput(デバイス入力由来)。
	// アトラクト(デモ)モードでは playerBrain_ がもう1体の AI として動かす(学習モデルは無し)。
	CharacterInput resolvedPlayerInput = playerInput;
	if (attractMode_ && playerBrain_) {
		resolvedPlayerInput = DecideAiInput(*playerBrain_, *player_, *enemy_, nullptr, dt);
	}

	player_->Update(dt, resolvedPlayerInput.moveX, resolvedPlayerInput.jumpTriggered, resolvedPlayerInput.crouchHeld,
		resolvedPlayerInput.aimDirX, resolvedPlayerInput.aimDirY, resolvedPlayerInput.attackTriggered,
		resolvedPlayerInput.attackHeld, resolvedPlayerInput.throwTriggered);
	enemy_->Update(dt, enemyInput.moveX, enemyInput.jumpTriggered, enemyInput.crouchHeld,
		enemyInput.aimDirX, enemyInput.aimDirY, enemyInput.attackTriggered,
		enemyInput.attackHeld, enemyInput.throwTriggered);

	// ジャンプの踏み切り・着地の瞬間に、キャラの足元にエフェクトを出す。
	auto playFootEffect = [](Character& c, const char* effectName) {
		const Vector3 center = c.GetColliderCenter();
		const Vector3 half = c.GetColliderHalfExtent();
		EffectManager::GetInstance()->Play(effectName, { center.x, center.y - half.y, center.z });
	};
	for (Character* c : { player_.get(), enemy_.get() }) {
		if (c->ConsumeJumpEffect()) playFootEffect(*c, "Jump");
		if (c->ConsumeLandEffect()) playFootEffect(*c, "Jump");
	}

	// プレイヤーの行動を観測(ポイントを取られるたびに敵が強くなるための土台)。
	playerModel_->Observe(*player_, *enemy_, stage_.get(), dt);

	//===================================
	// 当たり判定
	// キャラ同士の「押し合い」(すり抜け防止)はここで初めて実際に判定される。
	// 各キャラの Capsule コライダーは Character::SetupCollider() で登録済みで、
	// 重なりが見つかると Character::ResolveBodyBlock が自動で呼ばれる。
	// このシーンでは今のところどこからも呼んでいなかったため、Update() 内に追加してある。
	//===================================
	CollisionSystem::GetInstance()->Update();

	//===================================
	// ステージギミック(ベルトコンベア・トゲ即死・ポータル移動・爆風)
	//===================================
	UpdateStageGimmicks(dt);

	//===================================
	// 攻撃判定(素手)
	// CollisionSystem の毎フレーム総当たりには乗せず、両者ぶん明示的に ResolveAttack を呼ぶ。
	// 攻撃は「一瞬だけ判定が必要」なもので、常時判定する仕組みに乗せる必要がないため。
	//===================================
	ResolveAttack(*player_, *enemy_, "Player");
	ResolveAttack(*enemy_, *player_, "Enemy");

	//===================================
	// 銃弾・投げ捨てた武器の生成 → 更新・命中判定
	//===================================
	SpawnFromCharacter(*player_);
	SpawnFromCharacter(*enemy_);
	UpdateFlyingObjects(dt);
	UpdateFireHazards(dt);

	//===================================
	// 武器拾得・ランダムスポーン
	//===================================
	for (auto& pickup : pickups_) {
		pickup->Update(dt); // 着地するまでは重力で落下する(WeaponPickup.h の設計コメント参照)
		// 地面に置かれた武器もベルトコンベアで流れる。はみ出せば次フレームの落下判定で落ちる。
		if (stage_ && pickup->IsGrounded()) {
			const Vector3 pp = pickup->GetPosition();
			const Vector3 pickupHalf{ 0.25f, 0.15f, 0.25f }; // WeaponPickup::kHalfExtent と同じ
			const float sx = BeltShiftX(pp, pickupHalf, dt);
			if (sx != 0.0f) {
				pickup->SetPosition({ pp.x + sx, pp.y, pp.z });
			}
		}
	}
	TryPickUpWeapon(*player_);
	TryPickUpWeapon(*enemy_);
	UpdateWeaponSpawner(dt);

	//===================================
	// 被弾でコントローラーを弱く振動させる。
	// ダメージ源（殴り・銃弾・爆風・炎・トゲ）を問わず、プレイヤーの HP が前フレームより
	// 減っていたら被弾とみなす。爆発の中振動が来ているフレームは弱い被弾振動に上書きされない
	// （TriggerRumble 側で弱い要求は強さを据え置く）。
	//===================================
	if (!attractMode_ && player_) {
		const float hp = player_->GetHP();
		if (hp < prevPlayerHP_ - 0.01f) {
			TriggerRumble(kHitMotorLeft, kHitMotorRight, kHitRumbleSeconds);
		}
		prevPlayerHP_ = hp;
	}

	//===================================
	// 場外・HP0判定 → 得点(MatchRule)判定
	// 決着していなければ次のラウンド用にランダムなステージ切替を予約し(CheckKnockoutAndReset 内)、
	// 決着していれば Result シーンへ遷移する。
	//
	// チュートリアルには得点もラウンドもステージ切替も無いので、この一式は丸ごと
	// UpdateTutorial(説明送り・位置だけのリセット・完了判定)に差し替える。
	//===================================
	if (tutorialMode_) {
		UpdateTutorial(dt);
	} else {
	// 各キャラの「やられ方」を、リセット前に記録しておく。
	const bool enemyWasOutOfBounds = IsOutOfBounds(enemy_->GetPosition());
	const bool playerWasOutOfBounds = IsOutOfBounds(player_->GetPosition());
	const bool playerWasCrouching = player_->IsCrouching();
	const float playerDeathX = player_->GetPosition().x;
	float pePairDist = 0.0f;
	{
		const Vector3 pp = player_->GetPosition();
		const Vector3 pe = enemy_->GetPosition();
		const float dx = pp.x - pe.x;
		const float dy = pp.y - pe.y;
		pePairDist = std::sqrt(dx * dx + dy * dy);
	}

	const bool koPlayer = CheckKnockoutAndReset(*player_, MatchRule::Winner::Enemy, "Player");
	const bool koEnemy = CheckKnockoutAndReset(*enemy_, MatchRule::Winner::Player, "Enemy");
	if (koPlayer || koEnemy) {
		enemyBrain_->ResetForNewRound();
	}
	if (koEnemy) {
		// 敵が撃破/場外 = プレイヤーが1点。ここで敵が「学習」して強くなる。
		playerModel_->OnPointConceded();
		// 場外での自滅、またはトゲ踏みなら「危険地形に慎重になる」学習も進める。
		enemyBrain_->NotifyDeath(enemyWasOutOfBounds, enemyDeathBySpike_);
	}
	if (koPlayer) {
		// プレイヤーが撃破/場外 = 敵が1点。倒し方の傾向を学習する。
		PlayerModel::DefeatCause cause = playerWasOutOfBounds
			? PlayerModel::DefeatCause::OutOfBounds
			: (pePairDist < 2.5f ? PlayerModel::DefeatCause::Melee
				: PlayerModel::DefeatCause::Ranged);
		playerModel_->OnPlayerDefeated(cause, playerDeathX, playerWasCrouching);
	}
	} // if (tutorialMode_) else

	//===================================
	// デバッグ表示の残り時間を進める(実際の描画は Draw() 側)
	//===================================
	UpdateDebugFlashes(dt);

	//===================================
	// エフェクト(EffectManager / GPUParticle)の更新。
	// deltaTime は unscaled な実 delta を渡す(各コンポーネントの TimeGroup で内部スケールされる)。
	//===================================
	UpdateGlobalEffects(camera_.get(), dxCore_ ? dxCore_->GetDeltaTime() : dt);

	//===================================
	// タイトルへ戻る(アトラクトモードでは無効 ── タイトルそのものがこのデモなので)
	//===================================
	bool back = false;
	if (input_ && !attractMode_) {
		if (auto* kb = input_->GetKeyboard()) {
			back |= kb->TriggerKey(DIK_ESCAPE);
		}
		// チュートリアル中の (B) は「説明を次へ」なので、タイトル復帰には使わない
		// (キーボードの ESC でだけ抜けられる)。
		if (auto* pad = input_->GetController(); pad && !tutorialMode_) {
			back |= pad->IsButtonTriggered(XINPUT_GAMEPAD_B);
		}
	}
	if (back) {
		SceneManager::GetInstance()->ChangeScene("Title", TransitionType::Fade);
	}
}

void GameScene::UpdateTutorial(float dt) {
	if (!tutorial_ || tutorialFinished_) {
		return;
	}

	// 説明が武器の段に来た(または死亡リセットで素手に戻った)なら、拾える武器を1つ置く。
	if (tutorial_->ConsumeWeaponSpawnRequest()) {
		SpawnTutorialWeapon();
	}

	// やられた直後の猶予。死亡モーション/落下を少し見せてから位置だけ戻す。
	if (tutorialRespawnTimer_ > 0.0f) {
		tutorialRespawnTimer_ -= dt;
		if (tutorialRespawnTimer_ <= 0.0f) {
			ResetTutorialPositions();
		}
		return;
	}

	const bool playerDown = player_->IsDead() || IsOutOfBounds(player_->GetPosition());
	const bool enemyDown = enemy_->IsDead() || IsOutOfBounds(enemy_->GetPosition());

	// 全ての説明を終えた後に敵を倒した = チュートリアル完了。セーブして本編へ。
	if (enemyDown && !playerDown && tutorial_->IsAwaitingFinalKill()) {
		tutorialFinished_ = true;
		SaveData::SetTutorialCleared(true);
		Log("チュートリアル完了。本編へ進みます\n");
		// LoadStage を通らない経路なので、ここでも明示的に止めないと撃破エフェクトや
		// 振動が次のシーンへ持ち越される(UpdateRoundEnd の Result 遷移と同じ理由)。
		EffectManager::GetInstance()->StopAll();
		StopRumble();
		SceneManager::GetInstance()->ChangeScene("Game", TransitionType::Fade);
		return;
	}

	// それ以外のやられ方(ギミックでの自滅・説明の途中で敵を倒した等)は、得点も
	// ステージ切替もせずに位置だけ戻してやり直す。
	if (playerDown || enemyDown) {
		tutorialRespawnTimer_ = kTutorialRespawnDelay;
		return;
	}

	// 説明の送り。キーボードは SPACE、パッドは (B)
	// ((A) はジャンプ、(X) は攻撃と重なるため、空いている (B) を使う)。
	bool advance = false;
	if (input_) {
		if (auto* kb = input_->GetKeyboard()) {
			advance |= kb->TriggerKey(DIK_SPACE);
		}
		if (auto* pad = input_->GetController()) {
			advance |= pad->IsButtonTriggered(XINPUT_GAMEPAD_B);
		}
	}
	tutorial_->Update(advance);
}

void GameScene::ResetTutorialPositions() {
	// ステージは作り直さない ── 壊した床・起爆した爆弾などの破壊状況と、
	// 説明の進行段階(tutorial_)をそのまま維持するのがチュートリアルの仕様。
	// 戻すのは両キャラの位置・HP・状態異常と、その場に残っている飛翔物だけ。
	if (player_) {
		player_->ResetForNewRound(playerSpawn_);
		prevPlayerHP_ = player_->GetHP();
	}
	if (enemy_) {
		enemy_->ResetForNewRound(enemySpawn_);
	}
	if (tutorialBrain_) {
		tutorialBrain_->Reset();
	}
	playerInPortal_ = false;
	enemyInPortal_ = false;
	StopRumble();

	// 飛んでいる弾・地面の炎は持ち越さない(初期位置に残っていると即死ループになる)。
	flyingObjects_.clear();
	fireHazards_.clear();
	debugFlashes_.clear();

	// ResetForNewRound で素手に戻るので、武器の説明まで進んでいるのに拾える武器が
	// 1つも無い状態になったら置き直す(説明を読み返せなくなるのを防ぐ)。
	if (tutorial_ && tutorial_->HasReachedWeaponStep()) {
		bool available = false;
		for (const auto& pk : pickups_) {
			if (!pk->IsTaken()) {
				available = true;
				break;
			}
		}
		if (!available) {
			tutorial_->RequestWeaponSpawn();
		}
	}
}

void GameScene::SpawnTutorialWeapon() {
	if (!stage_) {
		return;
	}
	// 「自分のマスは空いていて、その真下のマスは地形(足場)」= 立てる床の上。
	// そのうちプレイヤー初期位置に一番近いセルへ置く(UpdateWeaponSpawner の候補選びと同じ条件で、
	// 抽選の代わりに最短距離で決めているだけ)。
	Vector3 best{};
	float bestDistSq = 1e18f;
	bool found = false;
	for (int cy = 0; cy < StageGrid::kRows - 1; ++cy) {
		for (int cx = 0; cx < StageGrid::kCols; ++cx) {
			if (stage_->IsSolidCell(cx, cy) || !stage_->IsSolidCell(cx, cy + 1)) {
				continue;
			}
			const Vector3 c = stage_->CellToWorldCenter(cx, cy);
			const float dx = c.x - playerSpawn_.x;
			const float dy = c.y - playerSpawn_.y;
			const float distSq = dx * dx + dy * dy;
			if (distSq < bestDistSq) {
				bestDistSq = distSq;
				best = c;
				found = true;
			}
		}
	}
	if (!found) {
		return;
	}
	auto pickup = std::make_unique<WeaponPickup>();
	pickup->Initialize(camera_.get(), object3DManager_, dxCore_, best,
		std::make_unique<Pistol>(), stage_.get());
	pickups_.push_back(std::move(pickup));
}

bool GameScene::IsPadConnected() const {
	if (!input_) {
		return false;
	}
	auto* pad = input_->GetController();
	return pad && pad->IsConnected();
}

void GameScene::ResolveAttack(Character& attacker, Character& defender, const char* attackerLabel) {
	// attacker が直前の Update() で攻撃を出していなければ ConsumePendingAttack が false を返すので
	// 何もしない。出していれば hitbox にヒットボックス情報が詰められる。
	Character::AttackHitbox hitbox;
	if (!attacker.ConsumePendingAttack(hitbox)) {
		return;
	}
	// 実際に「当たったかどうか」の幾何判定とダメージ・ノックバックの適用は
	// defender.ReceiveHit() の中で完結する。ここではその結果を見てログを出すだけ。
	const bool hit = defender.ReceiveHit(hitbox);
	if (hit) {
		Log(std::string(attackerLabel) + " の攻撃が命中\n");
	}
	// 攻撃判定がどこに出たか目視確認できるよう、一定時間だけ球のワイヤーフレームを表示する
	// (命中したら緑、外れたら黄色)。マウス照準の向きが合っているかの確認にも使える。
	AddDebugFlash(hitbox.center, hitbox.radius,
		hit ? Vector4{ 0.2f, 1.0f, 0.2f, 1.0f } : Vector4{ 1.0f, 0.9f, 0.1f, 1.0f });

	// 同じヒットボックスで「壊れる床」も削る(HP0 で破壊)。相手ヒットとは独立。
	const int broke = stage_->DamageSphere(hitbox.center, hitbox.radius, hitbox.damage);
	if (broke > 0) {
		Log(std::string(attackerLabel) + " が壊れる床を破壊(" + std::to_string(broke) + ")\n");
	}

	// 殴り攻撃が敵かオブジェクト(壊れる床)に当たったら、その腕の位置にヒットエフェクトと
	// パンチ音を出す(振っただけで何にも当たらなかった場合は鳴らさない)。
	if (hit || broke > 0) {
		EffectManager::GetInstance()->Play("meller", hitbox.center);
		const int pick = RandomGenerator::Instance().NextInt(0, kPunchSoundCount - 1);
		SoundManager::GetInstance()->Play3DSound(kPunchSoundNames[pick], hitbox.center);
	}
	// 攻撃が当たった爆弾ブロックは3秒信管が始まる(少しでも当たれば作動)。
	stage_->ArmBombsInSphere(hitbox.center, hitbox.radius);
}

bool GameScene::IsOutOfBounds(const Vector3& pos) const {
	// 場外判定はステージへ委譲する(左右の外、または床の穴から下へ落ちたら場外)。
	return !stage_ || !stage_->IsPointInsideBounds(pos);
}

bool GameScene::CheckKnockoutAndReset(Character& target, MatchRule::Winner otherSide, const char* targetLabel) {

	// target が生きていて、かつ場内にいるなら何も起きていない
	if (!target.IsDead() && !IsOutOfBounds(target.GetPosition())) {
		return false;
	}

	// ここに来た = target がHP0になったか、アリーナ外に出た(=やられた)
	Log(std::string(targetLabel) + " が撃破/場外。相手に1ポイント\n");

	// 撃破エフェクトはここで必ず1回だけ出す(HP0の直撃は各ヒット処理側でも出るが、
	// ノックバックで場外に落ちて決着するケース ── 実際のプレイではこちらの方が多い ──
	// には ResolveAttack/ResolveExplosion のどちらも通らず、今まで一切エフェクトが
	// 無かったため)。武器やHP0/場外の別を問わず、撃破という結果そのものに紐付ける。
	EffectManager::GetInstance()->Play("Block_Exprosion", target.GetPosition());

	const bool matchOver = matchRule_.AddPoint(otherSide);
	if (matchOver) {
		const char* winnerLabel = (matchRule_.GetWinner() == MatchRule::Winner::Player) ? "Player" : "Enemy";
		Log(std::string(winnerLabel) + " が" + std::to_string(MatchRule::kPointsToWin) + "ポイント先取\n");
	}

	// ここでは target をリスポーンさせない。HP0 なら CLIP_DEATH の死亡モーションが
	// (場外なら落下がそのまま)RoundEnd の猶予中に最後まで再生されるようにするため。
	// target/other の実際のリセットは、次のラウンドへ進むときの LoadStage に任せる
	// (matchRule_ が決着していれば Result シーンへ遷移するだけでそもそもリセット不要)。

	// すぐには次のステージ/Result へ進まず、「どちらが勝ったか」を見せる猶予(RoundEnd)を挟む。
	// 猶予明けの実際の処理(ステージ抽選 or Result 遷移)は UpdateRoundEnd が行う。
	roundState_ = RoundState::RoundEnd;
	roundEndRemaining_ = kRoundEndSeconds;
	roundEndActionTaken_ = false;
	roundEndWinnerSide_ = otherSide;
	roundEndMatchOver_ = matchOver;
	return true;
}

void GameScene::AddDebugFlash(const Vector3& pos, float radius, const Vector4& color, float duration) {
	debugFlashes_.push_back(DebugFlash{ pos, radius, color, duration });
}

void GameScene::UpdateDebugFlashes(float dt) {
	for (auto& flash : debugFlashes_) {
		flash.remaining -= dt;
	}
	debugFlashes_.erase(
		std::remove_if(debugFlashes_.begin(), debugFlashes_.end(),
			[](const DebugFlash& f) { return f.remaining <= 0.0f; }),
		debugFlashes_.end());
}

void GameScene::DrawDebugAids() {
#ifdef _DEBUG
	// 攻撃判定(爆発範囲・殴り範囲・弾ヒットなど)の可視化ワイヤーフレーム。
	// あくまで開発中の当たり確認用なので Release/Development には出さない。
	// 下の照準レイ/着弾点(マウスカーソル方向)は仕様上の常時表示なので残す。
	for (const auto& flash : debugFlashes_) {
		DebugDraw::Sphere(flash.position, flash.radius, flash.color, 12);
	}
#endif // _DEBUG

	// プレイヤーの照準方向を常時表示する(シアンのレイ)。マウス/右スティックの向きが
	// 意図通りワールドに反映されているかを目視確認するためのデバッグ表示。
	// 着弾点(逆投影で実際に計算されたワールド座標)にも小さい十字を出す。
	if (player_) {
		DebugDraw::Ray(player_->GetPosition(), { player_->GetAimDirX(), player_->GetAimDirY(), 0.0f },
			3.0f, { 0.2f, 1.0f, 1.0f, 1.0f });
		DebugDraw::Cross(lastAimWorldPoint_, 0.3f, { 1.0f, 0.2f, 1.0f, 1.0f });
	}
}

void GameScene::SpawnFromCharacter(Character& shooter) {
	// 銃を撃っていれば(ショットガンなら複数弾ぶん)、小さい球を弾として生成する。
	std::vector<ProjectileSpawnRequest> spawns;
	if (shooter.ConsumePendingProjectileSpawns(spawns)) {
		for (const ProjectileSpawnRequest& spawn : spawns) {
			SpawnFlyingObject(spawn, &shooter, PrimitiveInstance::PrimitiveType::Sphere,
				{ 0.2f, 0.2f, 0.2f }, "Bullet");
		}
	}

	// 武器を投げ捨てていれば、見た目が銃弾よりひと回り大きい箱として生成する。
	// 投げた武器の実体(残弾込み)も一緒に運ばせる ── 着弾後、残弾が残っていれば
	// UpdateFlyingObjects 側でその場に WeaponPickup として再配置するため。
	ProjectileSpawnRequest throwSpawn;
	std::unique_ptr<Weapon> thrownWeapon;
	if (shooter.ConsumePendingThrow(throwSpawn, thrownWeapon)) {
		SpawnFlyingObject(throwSpawn, &shooter, PrimitiveInstance::PrimitiveType::Box,
			{ 0.4f, 0.4f, 0.4f }, "ThrownWeapon", std::move(thrownWeapon));
	}
}

void GameScene::SpawnFlyingObject(const ProjectileSpawnRequest& spec, Character* owner,
	PrimitiveInstance::PrimitiveType visualType, const Vector3& visualScale, const char* name,
	std::unique_ptr<Weapon> thrownWeaponPayload) {
	// 銃弾・投げ武器どちらも ArcingProjectile 1つで表現しているので(クラス冒頭コメント参照)、
	// 見た目以外はここで分岐する必要が無い。投げ武器のときだけ実際の武器モデルを渡し、
	// 銃弾のときは modelDir を空にしてプリミティブ(球)表示にする。
	std::string modelDir, modelFile;
	if (thrownWeaponPayload) {
		modelDir = thrownWeaponPayload->GetModelDirectory();
		modelFile = thrownWeaponPayload->GetModelFileName();
	}

	auto obj = std::make_unique<ArcingProjectile>();
	obj->Initialize(camera_.get(), name, spec, owner, stage_.get(), visualType, visualScale,
		object3DManager_, dxCore_, modelDir, modelFile);
	if (thrownWeaponPayload) {
		obj->SetThrownWeaponPayload(std::move(thrownWeaponPayload));
	}
	flyingObjects_.push_back(std::move(obj));
}

void GameScene::UpdateFlyingObjects(float dt) {
	for (auto& obj : flyingObjects_) {
		obj->Update(dt);
		// 跳ね返り武器(グレネードランチャー・リコシェットライフル)が今フレーム壁/床に
		// 当たって跳ねたら、その音を鳴らす(bounceSoundName が空の弾は何も鳴らさない)。
		Vector3 bouncePos{};
		if (obj->ConsumeBounceEvent(bouncePos) && !obj->GetBounceSoundName().empty()) {
			SoundManager::GetInstance()->Play3DSound(obj->GetBounceSoundName(), bouncePos);
		}
		// 飛翔中の弾が爆弾ブロックに触れたら信管が始まる(直撃で消えなくても、掠めれば作動)。
		if (!obj->IsDead() && stage_) {
			stage_->ArmBombsInSphere(obj->GetPosition(), obj->GetRadius());
		}
		// 今フレーム、どちらかのキャラに直撃して死んだか(地形/寿命切れとは区別する)。
		// 炎銃の着弾点フレア(下のSpawnFireHazard呼び出し)を「キャラに直撃した場合は
		// 除外する」判断に使う ── 直撃した相手は既に burnDps/burnDuration で燃えるので、
		// そこへさらに地面の炎(範囲攻撃)まで残すと同じ1発で二重に効果が及んでしまうため。
		bool diedFromCharacterHit = false;
		if (!obj->IsDead()) {
			// 発射者自身には ArcingProjectile::TryHitCharacter 内で当たらないようになっている。
			obj->TryHitCharacter(*player_);
			obj->TryHitCharacter(*enemy_);
			// 近接センサー判定(proximityRadius>0の弾のみ意味を持つ。グレネードランチャー専用)。
			// 発射者自身はセンサー対象外(ArcingProjectile::TryProximityDetonate参照)。
			if (!obj->IsDead()) {
				obj->TryProximityDetonate(*player_);
				obj->TryProximityDetonate(*enemy_);
			}
			// ここに来た時点で dead_ になっていれば、地形/寿命切れではなく「今フレーム
			// どちらかのキャラに命中して消えた」ということ(Update() 内の地形/寿命判定は
			// このブロックへ来る前に既に弾いているため)。ショットガンのように1トリガーで
			// 複数弾出る武器で「実際に何発当たっているか」を目視確認できるようにする。
			if (obj->IsDead()) {
				diedFromCharacterHit = true;
				AddDebugFlash(obj->GetPosition(), 0.25f, Vector4{ 0.2f, 1.0f, 0.2f, 1.0f }, 0.3f);
				// 爆風武器(blastRadius>0)はこの直後の ResolveExplosion 側で "Block_Exprosion" を
				// 出すので、ここでは通常弾(格闘の meller と同じ役割のヒットエフェクト)のみ出す。
				// 二重に出さないための分岐。
				if (obj->GetBlastRadius() <= 0.0f) {
					EffectManager::GetInstance()->Play("meller", obj->GetPosition());
				}
			}
		}

		// 地形/場外による死は、上の命中判定が終わるまで ArcingProjectile 側で保留されている
		// (ArcingProjectile::HasPendingTerrainDeath 参照 ── 壁際に立つ相手を狙った弾が、
		// 着弾判定を理由に命中判定なしで消えてしまうのを防ぐため)。ここで初めて確定させる。
		// 上の TryHitCharacter で実際に命中していれば既に上書き済みなので何も起きない。
		obj->ResolvePendingTerrainDeath();

		// 上の TryHitCharacter で今フレーム命中して死んだ場合も含めて、死因を問わず
		// ここでまとめて後処理を行う(爆風武器は「地形に当たったから」ではなく
		// 「死んだから」爆発してほしいので、DiedOnTerrain 判定より後段にまとめている)。
		if (obj->IsDead()) {
			if (obj->GetBlastRadius() > 0.0f) {
				ResolveExplosion(*obj);
			} else if (obj->DiedOnTerrain() && stage_) {
				// 爆風を持たない通常弾は今まで通り、着弾点だけの小さい範囲を削る。
				stage_->DamageSphere(obj->GetPosition(), obj->GetRadius() * 1.5f, obj->GetDamage());
			}
			// 炎銃(FireGun)は着弾点に炎を残す。ただしキャラに直撃した場合は残さない ──
			// 直撃した相手は既に burnDps/burnDuration で燃えている(ArcingProjectile::TryHitCharacter
			// が組み立てる AttackHitbox 経由)ので、そこにさらに地面の炎(範囲攻撃)まで広げると
			// 1発で「直撃燃焼+範囲燃焼」の二重取りになってしまう。地形着弾・寿命切れのときだけ
			// 炎を残す(爆風と違い地形は削らない。「立ち入れない範囲を作る」武器であって
			// 「壊す」武器ではないため)。
			if (obj->GetSpawnsFireHazard() && !diedFromCharacterHit) {
				SpawnFireHazard(obj->GetPosition(), obj->GetFireHazardRadius(),
					obj->GetFireHazardDuration(), obj->GetFireHazardDps());
			}
		}
	}

	// 消滅した物のうち、投げた武器の積み荷(残弾込み)を持っているものは、
	// リストから取り除く前に中身を確認する。残弾が残っていればその場に
	// WeaponPickup として再配置し、また拾えるようにする(残弾0ならそのまま何も残さず失う)。
	for (auto& obj : flyingObjects_) {
		if (!obj->IsDead()) {
			continue;
		}
		std::unique_ptr<Weapon> droppedWeapon = obj->TakeThrownWeaponPayload();
		if (droppedWeapon && droppedWeapon->GetRemainingAmmo() > 0) {
			SoundManager::GetInstance()->Play3DSound("Drop", obj->GetPosition());
			auto pickup = std::make_unique<WeaponPickup>();
			pickup->Initialize(camera_.get(), object3DManager_, dxCore_, obj->GetPosition(), std::move(droppedWeapon), stage_.get());
			pickups_.push_back(std::move(pickup));
		}
		// droppedWeapon が nullptr(銃弾だった)か残弾0の場合は、ここでスコープを抜けて破棄される。
	}

	// 消滅した物をリストから取り除く。
	flyingObjects_.erase(
		std::remove_if(flyingObjects_.begin(), flyingObjects_.end(),
			[](const std::unique_ptr<ArcingProjectile>& obj) { return obj->IsDead(); }),
		flyingObjects_.end());
}

void GameScene::TriggerRumble(unsigned short left, unsigned short right, float seconds) {
	if (attractMode_) {
		return; // デモ中はプレイヤーが AI なので鳴らさない
	}
	// 再生中の振動より弱い要求では強さを据え置き、時間だけ必要に応じて延長する。
	const bool weakerThanCurrent =
		rumbleRemaining_ > 0.0f && left <= rumbleLeft_ && right <= rumbleRight_;
	if (!weakerThanCurrent) {
		rumbleLeft_ = left;
		rumbleRight_ = right;
	}
	rumbleRemaining_ = (std::max)(rumbleRemaining_, seconds);
	if (auto* pad = input_ ? input_->GetController() : nullptr) {
		pad->SetVibration(rumbleLeft_, rumbleRight_);
	}
}

void GameScene::TriggerExplosionRumble(const Vector3& center) {
	if (attractMode_ || !player_) {
		return;
	}
	// 爆発がプレイヤーの左右どちら側か（+ = 右）。真上・真下なら dx≈0。
	const float dx = center.x - player_->GetPosition().x;
	// |dx| が大きいほど「反対側」のモーターを弱める（0..1 に正規化）。
	const float t = (std::min)(std::fabs(dx) / kRumblePanDistance, 1.0f);
	const auto lerpMotor = [](unsigned short a, unsigned short b, float s) -> unsigned short {
		return static_cast<unsigned short>(a + (b - a) * s);
	};
	const unsigned short faded = lerpMotor(kExplosionMotorMid, kExplosionMotorLow, t);
	unsigned short left = kExplosionMotorMid;
	unsigned short right = kExplosionMotorMid;
	if (dx > 0.0f) {
		right = kExplosionMotorMid; // 爆発は右側 → 右モーターは中のまま
		left = faded;               // 左モーターは距離に応じて中→弱
	} else if (dx < 0.0f) {
		left = kExplosionMotorMid;
		right = faded;
	}
	TriggerRumble(left, right, kExplosionRumbleSeconds);
}

void GameScene::UpdateRumble(float dt) {
	if (rumbleRemaining_ <= 0.0f) {
		return;
	}
	rumbleRemaining_ -= dt;
	if (rumbleRemaining_ <= 0.0f) {
		StopRumble();
	}
}

void GameScene::StopRumble() {
	rumbleRemaining_ = 0.0f;
	rumbleLeft_ = 0;
	rumbleRight_ = 0;
	if (auto* pad = input_ ? input_->GetController() : nullptr) {
		pad->StopVibration();
	}
}

void GameScene::ResolveExplosion(const ArcingProjectile& obj) {
	const Vector3 center = obj.GetPosition();
	const float blastRadius = obj.GetBlastRadius();

	// 爆風の届く範囲そのものを目視確認できるよう、爆心を中心に blastRadius の球を
	// 少し長め(0.5秒)に表示する。通常の攻撃判定フラッシュ(kDebugFlashDuration=0.25秒)より
	// 長くしているのは、爆風は一瞬で消えるヒットボックスと違い「どこまで届いたか」を
	// 見て次の立ち回りを考えるための表示だから。
	AddDebugFlash(center, blastRadius, Vector4{ 1.0f, 0.45f, 0.05f, 1.0f }, 0.5f);

	// 見た目のエフェクトも出す(ステージの爆弾ブロックと同じ "Block_Exprosion" を流用)。
	// 今まではデバッグ用ワイヤーフレームしか出ておらず、武器の爆風による撃破が
	// 格闘の meller に対して見た目の演出だけ無いのは不自然だったための追加。
	EffectManager::GetInstance()->Play("Block_Exprosion", center);

	// 爆発音。武器側が explosionSoundName を指定していればそれを、指定が無ければ
	// 汎用の爆発音(Explosion_Default)を鳴らす(Blaster.cpp/GrenadeLauncher.cpp 参照)。
	const std::string& explosionSound = obj.GetExplosionSoundName();
	SoundManager::GetInstance()->Play3DSound(explosionSound.empty() ? "Explosion_Default" : explosionSound, center);
	TriggerExplosionRumble(center);

	// 地形は「着弾点だけ」ではなく爆風半径ぶんまとめて削る(通常弾の着弾チップ削りより
	// 広い範囲。直撃/地形当たり/寿命切れのどれで死んだかは問わない)。
	if (stage_) {
		const int broke = stage_->DamageSphere(center, blastRadius, obj.GetDamage());
		if (broke > 0) {
			Log("爆発で壊れる床を破壊(" + std::to_string(broke) + ")\n");
		}
		// 武器の爆風に巻き込まれた爆弾ブロックも信管が始まる。
		stage_->ArmBombsInSphere(center, blastRadius);
	}

	// 発射者自身を含む全キャラクターへ、距離減衰させたダメージ/ノックバックを適用する。
	// TryHitCharacter と違って owner を除外しない ── 「自分の爆風にも巻き込まれる」が
	// このカテゴリの武器のリスクリワードそのものなので、ここでは意図的に区別しない。
	ApplyBlastToCharacter(*player_, center, blastRadius, obj.GetDamage(), obj.GetKnockbackPower());
	ApplyBlastToCharacter(*enemy_, center, blastRadius, obj.GetDamage(), obj.GetKnockbackPower());
}

void GameScene::ApplyBlastToCharacter(Character& target, const Vector3& center, float blastRadius,
	float maxDamage, float maxKnockbackPower) {
	const float dx = target.GetPosition().x - center.x;
	const float dy = target.GetPosition().y - center.y;
	const float dist = std::sqrt(dx * dx + dy * dy);
	if (dist >= blastRadius) {
		return; // 爆風の範囲外
	}

	// 爆心(dist=0)で 1.0、爆風の端(dist=blastRadius)で 0.0 になる線形減衰。
	// 「近いほど強い」を素直に表現できればよいので、今のところこれ以上凝った
	// カーブ(二乗減衰など)にはしていない。
	const float falloff = 1.0f - (dist / blastRadius);

	Character::AttackHitbox hitbox;
	hitbox.center = center;
	hitbox.radius = blastRadius; // ReceiveHit 内の球vsカプセル判定にそのまま爆風半径を使う
	hitbox.damage = maxDamage * falloff;
	hitbox.knockbackPower = maxKnockbackPower * falloff;
	// 爆心から見て自分がどちら側にいるかで、外向きに吹き飛ぶ方向を決める
	// (弾の飛行方向を使う通常弾の knockbackDirX とは考え方が異なる点に注意)。
	hitbox.knockbackDirX = (dx >= 0.0f) ? 1.0f : -1.0f;

	target.ReceiveHit(hitbox);
}

void GameScene::SpawnFireHazard(const Vector3& center, float radius, float duration, float dps) {
	auto hazard = std::make_unique<FireHazard>();
	hazard->Initialize(camera_.get(), center, radius, dps, duration);
	fireHazards_.push_back(std::move(hazard));
	// 炎が居座る間ずっとループさせる仕組みは持たないため、着火の瞬間に単発で鳴らす。
	SoundManager::GetInstance()->Play3DSound("FireHazard_Ignite", center);
}

void GameScene::UpdateFireHazards(float dt) {
	for (auto& hazard : fireHazards_) {
		hazard->Update(dt);

		// 炎銃自身も含め、踏んでいるキャラは毎フレーム燃え続ける。ReceiveHit を経由しないので
		// ノックバックには一切影響しない(FireHazard.h の設計コメント参照)。ApplyBurn は
		// Character::ApplyKnockback と同じ上書き式なので、踏み続ける限り毎フレーム
		// 「あと1秒燃える」に更新され続け、離れた瞬間から自然に残り火が消えていく。
		constexpr float kBurnRefreshDuration = 1.0f;
		if (hazard->Overlaps(player_->GetPosition())) {
			player_->ApplyBurn(hazard->GetDps(), kBurnRefreshDuration);
		}
		if (hazard->Overlaps(enemy_->GetPosition())) {
			enemy_->ApplyBurn(hazard->GetDps(), kBurnRefreshDuration);
		}
	}

	fireHazards_.erase(
		std::remove_if(fireHazards_.begin(), fireHazards_.end(),
			[](const std::unique_ptr<FireHazard>& hazard) { return hazard->IsDead(); }),
		fireHazards_.end());
}

void GameScene::TryPickUpWeapon(Character& character) {
	if (!character.CanPickUpWeapon()) {
		return; // 既に何か武器を持っている(素手ではない)ので拾えない
	}
	for (auto& pickup : pickups_) {
		if (pickup->IsTaken()) {
			continue;
		}
		// 簡易な距離判定(Character のカプセル半径程度に触れたら拾える扱いにする)。
		constexpr float kPickupRadius = 1.0f;
		const Vector3 delta = {
			character.GetPosition().x - pickup->GetPosition().x,
			character.GetPosition().y - pickup->GetPosition().y,
			character.GetPosition().z - pickup->GetPosition().z
		};
		const float distSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
		if (distSq <= kPickupRadius * kPickupRadius) {
			character.EquipWeapon(pickup->TakeWeapon());
			return; // 1フレームに1つまで(複数拾いのバグ防止)
		}
	}
}

void GameScene::UpdateWeaponSpawner(float dt) {
	if (tutorialMode_) {
		// チュートリアルでは武器は勝手に湧かない。説明が武器の段に来たときに
		// SpawnTutorialWeapon() が1丁だけ置く(説明中に足元へ武器が降ってこないように)。
		return;
	}
	weaponSpawnTimer_ -= dt;
	if (weaponSpawnTimer_ > 0.0f) {
		return;
	}
	weaponSpawnTimer_ = kWeaponSpawnInterval;

	// WeaponPickup は物理演算をしない(位置固定)ので、床のあるマスを PickWeaponSpawnPosition で
	// 選んで渡さないと空中や壁の中に湧いてしまう。
	Vector3 spawnPos;
	if (!PickWeaponSpawnPosition(*stage_, &spawnPos)) {
		return; // 足場のあるステージでは通常起きないが、念のため
	}

	auto pickup = std::make_unique<WeaponPickup>();
	pickup->Initialize(camera_.get(), object3DManager_, dxCore_, spawnPos, CreateRandomWeapon(), stage_.get());
	pickups_.push_back(std::move(pickup));
}

void GameScene::SpawnSpecificWeaponPickup(int poolIndex) {
	if (poolIndex < 0 || poolIndex >= kWeaponSpawnPoolCount || !stage_) {
		return;
	}
	// UpdateWeaponSpawner と同じ候補地選びを使う。enabled チェックは見ない(デバッグ用の
	// 「今すぐこれを出したい」ボタンなので、Spawn Pool から外していても押せば出せてよい)。
	Vector3 spawnPos;
	if (!PickWeaponSpawnPosition(*stage_, &spawnPos)) {
		return;
	}
	auto pickup = std::make_unique<WeaponPickup>();
	pickup->Initialize(camera_.get(), object3DManager_, dxCore_, spawnPos, g_weaponSpawnPool[poolIndex].factory(), stage_.get());
	pickups_.push_back(std::move(pickup));
}

void GameScene::Draw() {
	if (background_) {
		background_->Draw();
	}

	//===================================
	// プリミティブ相当(ステージ・キャラ本体・弾)。
	// 各 Draw() が内部で PrimitivePipeline を貼り直すので順序の制約は無い。
	//===================================
	if (stage_) stage_->Draw();
	if (player_) player_->Draw();
	if (enemy_) enemy_->Draw();
	for (auto& pickup : pickups_) {
		pickup->Draw();                 // モデルが読めなかったときのフォールバックの箱のみ
	}
	for (auto& obj : flyingObjects_) {
		obj->Draw();                    // 銃弾のプリミティブのみ(投げ武器モデルは下の Object3D パス)
	}
	for (auto& hazard : fireHazards_) {
		hazard->Draw();                 // 炎銃(FireGun)が着弾点に残す炎
	}

	//===================================
	// 武器モデル(Object3D パス)。
	// Object3DManager::DrawSetting でルートシグネチャ/PSO/シャドウ/フォグを、
	// LightManager::BindLights で b1/b3/b4(平行光源/点光源/スポット)をまとめて設定してから
	// 各 Object3DInstance を描く(CG2_0_1 StagePlayScene::Draw と同じ順序。
	// Object3DInstance::Draw はライトを bind しないため、ここで一括設定しないと GBV #935 になる)。
	//===================================
	if (object3DManager_ && dxCore_) {
		object3DManager_->DrawSetting();
		LightManager::GetInstance()->BindLights(dxCore_->GetCommandList());
		if (titleLogo_) titleLogo_->Draw(dxCore_); // アトラクト時のタイトルロゴ(Title.mesh)
		if (stage_) stage_->DrawModels(dxCore_); // ステージギミックのトゲ(Spike.mesh)
		for (auto& pickup : pickups_) {
			pickup->DrawModel(dxCore_);
		}
		for (auto& obj : flyingObjects_) {
			obj->DrawModel(dxCore_);
		}
		// キャラ本体(スキニングモデル)。スキニング Compute の Dispatch → 描画 の順で呼ぶ。
		if (player_) { player_->DispatchAnimatedSkinning(dxCore_); player_->DrawAnimatedModel(dxCore_); }
		if (enemy_)  { enemy_->DispatchAnimatedSkinning(dxCore_);  enemy_->DrawAnimatedModel(dxCore_); }
		if (player_) player_->DrawWeaponModel(dxCore_);
		if (enemy_) enemy_->DrawWeaponModel(dxCore_);
	}

	//===================================
	// エフェクト(EffectManager)の描画。専用パイプラインを内部で貼るので順序制約は無いが、
	// 世界の上に乗せたいのでキャラ・武器モデルの後に描く。
	//===================================
	DrawGlobalEffects();

	//===================================
	// デバッグ線の描画。
	// DebugDraw::* は線をキューに積むだけなので、
	// 最後に LineRenderer::Draw() を呼ばないと何も出ない(エンジンの定番の落とし穴)。
	//===================================
	DrawDebugAids();

	auto* lr = LineRenderer::GetInstance();
	lr->SetCamera(GetCamera()); // カメラ未設定だと線の描画位置が定まらないので必須
	lr->Draw();

	//===================================
	// 画面左上のデバッグ表示(操作方法・HP・仮の得点)。
	// 本物のUI(フェーズ5)が入るまでの動作確認用。
	//===================================
	auto* tr = TextRenderer::GetInstance();
	if (tr && tr->IsInitialized()) {
		// ラウンド開始前の3秒カウントダウン、決着直後の「どちらが勝ったか」を
		// 画面中央に大きく出す(TitleScene::Draw と同じセンタリング)。
		// アトラクト(デモ)モードでは START ガイド以外の文字は一切出さない。
		if (!attractMode_ && roundState_ == RoundState::Countdown) {
			char cd[8];
			snprintf(cd, sizeof(cd), "%d", static_cast<int>(std::ceil(countdownRemaining_)));
			const float w = static_cast<float>(WindowsApplication::kClientWidth);
			const float cw = tr->MeasureWidth(cd, 3.0f);
			tr->DrawText(cd, { (w - cw) * 0.5f, 260.0f }, 3.0f);
		} else if (!attractMode_ && roundState_ == RoundState::RoundEnd) {
			const char* winnerText = (roundEndWinnerSide_ == MatchRule::Winner::Player)
				? "Player Wins the Round!"
				: "Enemy Wins the Round!";
			const float w = static_cast<float>(WindowsApplication::kClientWidth);
			const float ww = tr->MeasureWidth(winnerText, 2.0f);
			tr->DrawText(winnerText, { (w - ww) * 0.5f, 260.0f }, 2.0f);

			// バナーの下に、このラウンドぶんを加算したあとの取得ラウンド数を出す
			// (加点は RoundEnd へ移る前に matchRule_ へ入っている)。
			// 数字はキャラクターモデルと同じ色。レイアウトは ResultScene と揃えてある。
			MatchScoreText::DrawCenteredScore(tr,
				matchRule_.GetPlayerPoints(), matchRule_.GetEnemyPoints(),
				w * 0.5f, 350.0f, 2.4f);
			MatchScoreText::DrawCenteredPair(tr, "PLAYER", "ENEMY", w * 0.5f, 430.0f, 0.8f);
		}

		// アトラクト(デモ)モード = タイトル画面。本編への入り方を画面下中央に大きく出す。
		// 背景に埋もれないよう黒アウトライン付き。
		if (attractMode_) {
			const float w = static_cast<float>(WindowsApplication::kClientWidth);
			const char* guide = "PRESS SPACE or (A)";
			constexpr float kGuideScale = 2.2f;
			const float gw = tr->MeasureWidth(guide, kGuideScale);
			tr->DrawText(guide, { (w - gw) * 0.5f, 760.0f }, kGuideScale,
				{ 1.0f, 1.0f, 1.0f, 1.0f },   // 白
				3.0f,                          // アウトライン太さ
				{ 0.0f, 0.0f, 0.0f, 1.0f });   // 黒アウトライン
		}

		// チュートリアルの説明パネル(画面下)。
		DrawTutorialGuide();

#ifdef _DEBUG
		// ここから下は動作確認用のデバッグ表示(操作説明・HP・得点・AI内部状態)。
		// 本物のUI(フェーズ5)が入るまでの仮表示なので、Release/Development には出さない。
		// アトラクト(デモ)モードでは START ガイド以外は出さないので、これも丸ごと省く。
		if (!attractMode_) {
		tr->DrawText("A/D : Move   W/A(pad) : Jump   S/Down(pad) : Crouch", { 32.0f, 32.0f }, 0.8f);
		tr->DrawText("Mouse/RStick : Aim   LClick/RT(pad) : Attack   R/RClick/Y(pad) : Throw", { 32.0f, 64.0f }, 0.8f);
		tr->DrawText("ESC / (B) : Title", { 32.0f, 96.0f }, 0.8f);

		char hpLine[128];
		snprintf(hpLine, sizeof(hpLine), "Player HP: %.0f   Enemy HP: %.0f", player_->GetHP(), enemy_->GetHP());
		tr->DrawText(hpLine, { 32.0f, 128.0f }, 0.8f);

		char pointLine[128];
		snprintf(pointLine, sizeof(pointLine), "Points  Player: %d   Enemy: %d",
			matchRule_.GetPlayerPoints(), matchRule_.GetEnemyPoints());
		tr->DrawText(pointLine, { 32.0f, 160.0f }, 0.8f);

		// 敵 AI の状態と学習ティア(デバッグ表示。本番 UI は B)。
		char aiLine[160];
		const int enemyAmmo = enemy_->GetEquippedAmmo();
		snprintf(aiLine, sizeof(aiLine), "Enemy AI: %s   Weapon: %s(%d)   Tier: %d   FallCaution: %d",
			enemyBrain_->GetStateName(), enemy_->GetEquippedWeaponName().c_str(), enemyAmmo,
			playerModel_->Tier(), enemyBrain_->GetFallCaution());
		tr->DrawText(aiLine, { 32.0f, 224.0f }, 0.8f);

		char obsLine[160];
		snprintf(obsLine, sizeof(obsLine), "Observed  Jump/s: %.2f  Crouch: %.0f%%  Camp: %s",
			playerModel_->JumpsPerSecond(), playerModel_->CrouchRatio() * 100.0f,
			playerModel_->LikesCampingBreakable() ? "yes" : "no");
		tr->DrawText(obsLine, { 32.0f, 256.0f }, 0.8f);

		const EnemyBrain::Debug d = enemyBrain_->GetDebug();
		char dbgLine[192];
		snprintf(dbgLine, sizeof(dbgLine),
			"AIdbg move:%.1f edgeBias:%.0f blocked:%d fetch:%d bl:%d pkDist:%.1f frozen:%.1f",
			d.moveX, d.edgeBias, d.terrainBlocked ? 1 : 0, d.wantFetch ? 1 : 0,
			d.blacklisted ? 1 : 0, d.pickupDist, d.frozen);
		tr->DrawText(dbgLine, { 32.0f, 288.0f }, 0.7f);

		char learnLine[192];
		const int pushDir = playerModel_->PreferredPushDir();
		snprintf(learnLine, sizeof(learnLine),
			"Learned  Lead:%.2f Dodge:%.2f Spacing:%.2f | Push:%s Close:%d Ranged:%d",
			playerModel_->LeadFactor(), playerModel_->DodgeSkill(), playerModel_->SpacingSkill(),
			pushDir > 0 ? "R" : (pushDir < 0 ? "L" : "-"),
			playerModel_->PrefersCloseCombat() ? 1 : 0,
			playerModel_->PrefersRangedKeepaway() ? 1 : 0);
		tr->DrawText(learnLine, { 32.0f, 316.0f }, 0.7f);

		// 装備中の武器名と残弾(素手など弾の概念が無い武器は kInfiniteAmmo なので数値を出さない)。
		char weaponLine[128];
		const int ammo = player_->GetEquippedAmmo();
		if (ammo == Weapon::kInfiniteAmmo) {
			snprintf(weaponLine, sizeof(weaponLine), "Weapon: %s", player_->GetEquippedWeaponName().c_str());
		} else {
			snprintf(weaponLine, sizeof(weaponLine), "Weapon: %s (Ammo: %d)", player_->GetEquippedWeaponName().c_str(), ammo);
		}
		tr->DrawText(weaponLine, { 32.0f, 192.0f }, 0.8f);
		} // if (!attractMode_)
#endif

		tr->Flush(); // スプライトと同じタイミング(描画順の最後)で確定させる
	}
}

void GameScene::DrawTutorialGuide() {
	if (!tutorialMode_ || !tutorial_) {
		return;
	}
	auto* tr = TextRenderer::GetInstance();
	if (!tr || !tr->IsInitialized()) {
		return;
	}

	// コントローラーが繋がっていればパッドのキー表記、無ければキーボード＆マウスの表記を出す。
	const bool pad = IsPadConnected();
	const TutorialStep& step = tutorial_->CurrentStep();
	const char* body = pad ? step.bodyPad : step.bodyKeyboard;
	const char* hint = step.waitForKill
		? "敵を倒すとチュートリアル終了"
		: (pad ? "(B) ボタンで次へ" : "SPACE キーで次へ");

	const float screenW = static_cast<float>(WindowsApplication::kClientWidth);
	// 画面幅からこれだけ内側に収める(左右の余白)。
	constexpr float kMargin = 60.0f;
	const float maxW = screenW - kMargin * 2.0f;

	// 指定スケールで入りきらない行は、幅に収まるところまで自動で縮める
	// (本文の長さがステップごとに違うので、決め打ちのスケールだと画面外へはみ出す)。
	auto fitScale = [&](const char* text, float desiredScale) {
		const float w = tr->MeasureWidth(text, desiredScale);
		return (w > maxW && w > 0.0f) ? desiredScale * (maxW / w) : desiredScale;
	};
	// 中央寄せで1行描く(背景に埋もれないよう黒アウトライン付き。START ガイドと同じ扱い)。
	auto drawCentered = [&](const char* text, float y, float scale, const Vector4& color) {
		const float s = fitScale(text, scale);
		const float w = tr->MeasureWidth(text, s);
		tr->DrawText(text, { (screenW - w) * 0.5f, y }, s, color,
			3.0f, { 0.0f, 0.0f, 0.0f, 1.0f });
	};

	// 見出しには「3 / 12」の進行度を添える(あと何回 SPACE を押すかの目安)。
	char title[192];
	snprintf(title, sizeof(title), "[%d/%d] %s",
		tutorial_->StepIndex() + 1, tutorial_->StepCount(), step.title);

	drawCentered(title, 690.0f, 1.3f, { 1.0f, 0.92f, 0.35f, 1.0f }); // 見出し = 黄
	drawCentered(body, 750.0f, 0.95f, { 1.0f, 1.0f, 1.0f, 1.0f });   // 本文 = 白
	drawCentered(hint, 810.0f, 0.9f, { 0.65f, 0.9f, 1.0f, 1.0f });   // 進行の案内 = 水色
}
