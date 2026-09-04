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
#include "Log.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
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

	/// <summary>0〜(count-1) の乱数で武器の種類を選び、フル装弾で1つ生成する。RandomGenerator 経由なので
	/// リプレイのシード再現性を壊さない(11_Utilities.md、生の rand() は使わない)。</summary>
	std::unique_ptr<Weapon> CreateRandomWeapon() {
		const int kind = RandomGenerator::Instance().NextInt(0, 4);
		switch (kind) {
			case 0: return std::make_unique<Pistol>();
			case 1: return std::make_unique<AssaultRifle>();
			case 2: return std::make_unique<Shotgun>();
            case 3: return std::make_unique<Blaster>();
			case 4: return std::make_unique<GrenadeLauncher>();
			default: return 0;
		}
	}
}

void GameScene::Initialize() {
	//===================================
	// カメラ
	//===================================
	// 32x18 セルのステージ全体を横から見る固定視点。値は仮置きで、
	// デバッグカメラ(Scene 基底機能)で追い込んでから確定する。
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 8.0f, -38.0f });
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
	// ステージ(CSV マップチップ)
	// 10=黒 / 20=白 の 1.0f 立方体で描画。1/2 のマスからスポーン座標を取り出す。
	// CSV が読めない場合は最下段だけ床にしたフォールバックで起動する。
	//===================================
	stage_ = std::make_unique<StageGrid>();
	stage_->LoadFromCsv(kStageCsvPath);
	stage_->Initialize(camera_.get());

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
	player_->SetWeaponRenderContext(object3DManager_, dxCore_);

	enemy_ = std::make_unique<Character>();
	enemy_->Initialize(camera_.get(), "Enemy", enemySpawn_);
	enemy_->SetStage(stage_.get());
	enemy_->SetWeaponRenderContext(object3DManager_, dxCore_);

	// 敵 AI と学習モデル。GameScene は Think() の結果を Character へ渡すだけ。
	enemyBrain_ = std::make_unique<EnemyBrain>();
	enemyBrain_->Initialize(kEnemyTurretMode);
	playerModel_ = std::make_unique<PlayerModel>();
	playerModel_->Reset();

	playerPoints_ = 0;
	enemyPoints_ = 0;

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
		});
	}
#endif
}

void GameScene::Finalize() {
	// 依存関係はないが、生成順と逆順に破棄する(可読性のための慣習)。
	pickups_.clear();
	flyingObjects_.clear();
	playerModel_.reset();
	enemyBrain_.reset();
	enemy_.reset();
	player_.reset();
	stage_.reset();
	camera_.reset();
}

void GameScene::Update() {
	// デバッグカメラが有効ならそちらの行列をシーンカメラへ注入する(Scene基底の機能)。
	// 無効なら通常どおり自前のカメラを更新する。
	UpdateDebugCameraIfActive();
	if (!GetUseDebugCamera()) {
		camera_->Update();
	}

	// ゲームロジックは Player グループの時間で進める。
	// ヒットストップやスローを入れるときにここが効く
	const float dt = GetScaledDeltaTime(TimeGroup::Player);

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

	stage_->Update();

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

	// 敵の意図は EnemyBrain が決める(入力デバイスは一切読まない)。
	// 敵が素手のとき拾いに行けるよう、取得可能で最寄りの武器 pickup を渡す。
	// 「真上の別プラットフォームにあって歩いても跳んでも届かない」もの、
	// および敵 AI が「届かない」と判断して避けているものは候補から除く。
	Vector3 nearestPickupPos{};
	bool hasNearestPickup = false;
	{
		const Vector3 ep = enemy_->GetPosition();
		const EnemyBrain::PickupAvoid avoid = enemyBrain_->GetPickupAvoid();
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

	// 敵に向かって飛んでくる弾（プレイヤーが撃ったもの）を探す。回避判断に使う。
	Vector3 threatPos{};
	Vector3 threatVel{};
	float threatTtc = 0.0f;
	bool threatActive = false;
	{
		const Vector3 ep = enemy_->GetPosition();
		float bestTtc = 1e9f;
		for (const auto& obj : flyingObjects_) {
			if (obj->IsDead() || obj->GetOwner() == enemy_.get()) {
				continue; // 自分の弾は脅威じゃない
			}
			const Vector3 pp = obj->GetPosition();
			const Vector3 pv = obj->GetVelocity();
			const float dx = ep.x - pp.x;
			if (dx * pv.x <= 0.0f || std::fabs(pv.x) < 1.0f) {
				continue; // 敵の方へ向かっていない
			}
			const float ttc = dx / pv.x;
			// 到達時点の弾の高さが敵の胴体あたりを通るか（ざっくり）。
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
	brainCtx.self = enemy_.get();
	brainCtx.target = player_.get();
	brainCtx.stage = stage_.get();
	brainCtx.playerModel = playerModel_.get();
	brainCtx.nearestPickup = hasNearestPickup ? &nearestPickupPos : nullptr;
	brainCtx.incomingThreat = threatActive;
	brainCtx.threatPos = threatPos;
	brainCtx.threatVel = threatVel;
	brainCtx.threatTtc = threatTtc;
	brainCtx.dt = dt;
	const CharacterInput enemyInput = enemyBrain_->Think(brainCtx);

	player_->Update(dt, playerInput.moveX, playerInput.jumpTriggered, playerInput.crouchHeld,
		playerInput.aimDirX, playerInput.aimDirY, playerInput.attackTriggered,
		playerInput.attackHeld, playerInput.throwTriggered);
	enemy_->Update(dt, enemyInput.moveX, enemyInput.jumpTriggered, enemyInput.crouchHeld,
		enemyInput.aimDirX, enemyInput.aimDirY, enemyInput.attackTriggered,
		enemyInput.attackHeld, enemyInput.throwTriggered);

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

	//===================================
	// 武器拾得・ランダムスポーン
	//===================================
	for (auto& pickup : pickups_) {
		pickup->Update(dt); // 着地するまでは重力で落下する(WeaponPickup.h の設計コメント参照)
	}
	TryPickUpWeapon(*player_);
	TryPickUpWeapon(*enemy_);
	UpdateWeaponSpawner(dt);

	//===================================
	// 場外・HP0判定 → 仮の得点+その場リセット
	// 本物の「10ポイント先取・次ステージへ自動遷移」といったラウンド進行はフェーズ5の別タスク。
	// ここでは「テストを継続できること」を優先して、即座にリセットするだけにしている。
	//===================================
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

	const bool koPlayer = CheckKnockoutAndReset(*player_, *enemy_, enemyPoints_, playerSpawn_, "Player");
	const bool koEnemy = CheckKnockoutAndReset(*enemy_, *player_, playerPoints_, enemySpawn_, "Enemy");
	if (koPlayer || koEnemy) {
		enemyBrain_->ResetForNewRound();
	}
	if (koEnemy) {
		// 敵が撃破/場外 = プレイヤーが1点。ここで敵が「学習」して強くなる。
		playerModel_->OnPointConceded();
		// 場外での自滅なら「穴に慎重になる」学習も進める。
		enemyBrain_->NotifyDeath(enemyWasOutOfBounds);
	}
	if (koPlayer) {
		// プレイヤーが撃破/場外 = 敵が1点。倒し方の傾向を学習する。
		PlayerModel::DefeatCause cause = playerWasOutOfBounds
			? PlayerModel::DefeatCause::OutOfBounds
			: (pePairDist < 2.5f ? PlayerModel::DefeatCause::Melee
				: PlayerModel::DefeatCause::Ranged);
		playerModel_->OnPlayerDefeated(cause, playerDeathX, playerWasCrouching);
	}

	//===================================
	// デバッグ表示の残り時間を進める(実際の描画は Draw() 側)
	//===================================
	UpdateDebugFlashes(dt);

	//===================================
	// タイトルへ戻る
	//===================================
	bool back = false;
	if (input_) {
		if (auto* kb = input_->GetKeyboard()) {
			back |= kb->TriggerKey(DIK_ESCAPE);
		}
		if (auto* pad = input_->GetController()) {
			back |= pad->IsButtonTriggered(XINPUT_GAMEPAD_B);
		}
	}
	if (back) {
		SceneManager::GetInstance()->ChangeScene("Title", TransitionType::Fade);
	}
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
}

bool GameScene::IsOutOfBounds(const Vector3& pos) const {
	// 場外判定はステージへ委譲する(左右の外、または床の穴から下へ落ちたら場外)。
	return !stage_ || !stage_->IsPointInsideBounds(pos);
}

bool GameScene::CheckKnockoutAndReset(Character& target, Character& other,
	int& otherPoints, const Vector3& targetRespawn, const char* targetLabel) {

	// target が生きていて、かつ場内にいるなら何も起きていない
	if (!target.IsDead() && !IsOutOfBounds(target.GetPosition())) {
		return false;
	}

	// ここに来た = target がHP0になったか、アリーナ外に出た(=やられた)
	otherPoints += 1;
	Log(std::string(targetLabel) + " が撃破/場外。相手に1ポイント\n");

	// 本物のラウンド進行(10ポイント先取・次ステージ選出)はフェーズ5の別タスク。
	// ここではテストを継続できるよう、その場で両者をリセットするだけにしている
	// (other は今の位置のまま、HPと速度だけ初期化してリスタートさせる)。
	target.ResetForNewRound(targetRespawn);
	other.ResetForNewRound(other.GetPosition());

	// 壊れた床も元に戻して、次のラウンドを同じ地形で始められるようにする。
	if (stage_) {
		stage_->ResetTerrain();
	}
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
	for (const auto& flash : debugFlashes_) {
		DebugDraw::Sphere(flash.position, flash.radius, flash.color, 12);
	}

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
				AddDebugFlash(obj->GetPosition(), 0.25f, Vector4{ 0.2f, 1.0f, 0.2f, 1.0f }, 0.3f);
			}
		}

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

void GameScene::ResolveExplosion(const ArcingProjectile& obj) {
	const Vector3 center = obj.GetPosition();
	const float blastRadius = obj.GetBlastRadius();

	// 爆風の届く範囲そのものを目視確認できるよう、爆心を中心に blastRadius の球を
	// 少し長め(0.5秒)に表示する。通常の攻撃判定フラッシュ(kDebugFlashDuration=0.25秒)より
	// 長くしているのは、爆風は一瞬で消えるヒットボックスと違い「どこまで届いたか」を
	// 見て次の立ち回りを考えるための表示だから。
	AddDebugFlash(center, blastRadius, Vector4{ 1.0f, 0.45f, 0.05f, 1.0f }, 0.5f);

	// 地形は「着弾点だけ」ではなく爆風半径ぶんまとめて削る(通常弾の着弾チップ削りより
	// 広い範囲。直撃/地形当たり/寿命切れのどれで死んだかは問わない)。
	if (stage_) {
		const int broke = stage_->DamageSphere(center, blastRadius, obj.GetDamage());
		if (broke > 0) {
			Log("爆発で壊れる床を破壊(" + std::to_string(broke) + ")\n");
		}
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
	weaponSpawnTimer_ -= dt;
	if (weaponSpawnTimer_ > 0.0f) {
		return;
	}
	weaponSpawnTimer_ = kWeaponSpawnInterval;

	// 「自分のマスは空いていて、その真下のマスは地形(足場)」なセルだけを候補にする。
	// WeaponPickup は物理演算をしない(位置固定)ので、こうしておかないと空中や
	// 壁の中に湧いてしまう。cy はCSVの行番号で、値が大きいほどワールドでは下(WorldToCell参照)。
	std::vector<Vector3> candidates;
	for (int cy = 0; cy < StageGrid::kRows - 1; ++cy) {
		for (int cx = 0; cx < StageGrid::kCols; ++cx) {
			if (!stage_->IsSolidCell(cx, cy) && stage_->IsSolidCell(cx, cy + 1)) {
				candidates.push_back(stage_->CellToWorldCenter(cx, cy));
			}
		}
	}
	if (candidates.empty()) {
		return; // 足場のあるステージでは通常起きないが、念のため
	}

	auto& rng = RandomGenerator::Instance();
	const Vector3 spawnPos = candidates[static_cast<size_t>(rng.NextInt(0, static_cast<int>(candidates.size()) - 1))];

	auto pickup = std::make_unique<WeaponPickup>();
	pickup->Initialize(camera_.get(), object3DManager_, dxCore_, spawnPos, CreateRandomWeapon(), stage_.get());
	pickups_.push_back(std::move(pickup));
}

void GameScene::Draw() {
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
		for (auto& pickup : pickups_) {
			pickup->DrawModel(dxCore_);
		}
		for (auto& obj : flyingObjects_) {
			obj->DrawModel(dxCore_);
		}
		if (player_) player_->DrawWeaponModel(dxCore_);
		if (enemy_) enemy_->DrawWeaponModel(dxCore_);
	}

	//===================================
	// デバッグ線の描画。
	// DebugDraw::* は線をキューに積むだけなので、
	// 最後に LineRenderer::Draw() を呼ばないと何も出ない(エンジンの定番の落とし穴)。
	//===================================
	DebugDraw::Grid({ 0.0f, 0.01f, 0.0f }, 20.0f, 1.0f, { 0.4f, 0.4f, 0.5f, 1.0f });
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
		tr->DrawText("A/D : Move   W/A(pad) : Jump   S/Down(pad) : Crouch", { 32.0f, 32.0f }, 0.8f);
		tr->DrawText("Mouse/RStick : Aim   LClick/RT(pad) : Attack   R/RClick/Y(pad) : Throw", { 32.0f, 64.0f }, 0.8f);
		tr->DrawText("ESC / (B) : Title", { 32.0f, 96.0f }, 0.8f);

		char hpLine[128];
		snprintf(hpLine, sizeof(hpLine), "Player HP: %.0f   Enemy HP: %.0f", player_->GetHP(), enemy_->GetHP());
		tr->DrawText(hpLine, { 32.0f, 128.0f }, 0.8f);

		char pointLine[128];
		snprintf(pointLine, sizeof(pointLine), "Points  Player: %d   Enemy: %d", playerPoints_, enemyPoints_);
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

		tr->Flush(); // スプライトと同じタイミング(描画順の最後)で確定させる
	}
}
