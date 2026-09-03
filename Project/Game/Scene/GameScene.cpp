#include "GameScene.h"

#include "SceneManager.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "MouseInput.h"
#include "ControllerInput.h"
#include "Object3DManager.h"
#include "LightManager.h"
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
#include "Log.h"

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
		const int kind = RandomGenerator::Instance().NextInt(0, 2);
		switch (kind) {
			case 0: return std::make_unique<Pistol>();
			case 1: return std::make_unique<AssaultRifle>();
			default: return std::make_unique<Shotgun>();
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
	// player_ は操作キャラ、dummy_ は殴る練習台(静止したまま動かない)。
	// 本物の対戦AIはフェーズ4の別タスクなので、今は dummy_ で代用している。
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

	dummy_ = std::make_unique<Character>();
	dummy_->Initialize(camera_.get(), "Dummy", enemySpawn_);
	dummy_->SetStage(stage_.get());

	playerPoints_ = 0;
	dummyPoints_ = 0;
}

void GameScene::Finalize() {
	// 依存関係はないが、生成順と逆順に破棄する(可読性のための慣習)。
	pickups_.clear();
	flyingObjects_.clear();
	dummy_.reset();
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

	player_->Update(dt, moveX, jumpTriggered, crouchHeld, aimDir.x, aimDir.y, attackTriggered, attackHeld, throwTriggered);
	// 的は入力なしで呼ぶだけ(重力等の物理更新は必要なので Update 自体は呼ぶ)。照準は右向き固定。
	dummy_->Update(dt, 0.0f, false, false, 1.0f, 0.0f, false, false, false);

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
	ResolveAttack(*player_, *dummy_, "Player");
	ResolveAttack(*dummy_, *player_, "Dummy");

	//===================================
	// 銃弾・投げ捨てた武器の生成 → 更新・命中判定
	//===================================
	SpawnFromCharacter(*player_);
	SpawnFromCharacter(*dummy_);
	UpdateFlyingObjects(dt);

	//===================================
	// 武器拾得・ランダムスポーン
	//===================================
	for (auto& pickup : pickups_) {
		pickup->Update(); // 位置は動かないが、WVP計算のため毎フレーム呼ぶ必要がある
	}
	TryPickUpWeapon(*player_);
	TryPickUpWeapon(*dummy_);
	UpdateWeaponSpawner(dt);

	//===================================
	// 場外・HP0判定 → 仮の得点+その場リセット
	// 本物の「10ポイント先取・次ステージへ自動遷移」といったラウンド進行はフェーズ5の別タスク。
	// ここでは「テストを継続できること」を優先して、即座にリセットするだけにしている。
	//===================================
	CheckKnockoutAndReset(*player_, *dummy_, dummyPoints_, playerSpawn_, "Player");
	CheckKnockoutAndReset(*dummy_, *player_, playerPoints_, enemySpawn_, "Dummy");

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

void GameScene::CheckKnockoutAndReset(Character& target, Character& other,
	int& otherPoints, const Vector3& targetRespawn, const char* targetLabel) {

	// target が生きていて、かつ場内にいるなら何も起きていない
	if (!target.IsDead() && !IsOutOfBounds(target.GetPosition())) {
		return;
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
	// 見た目(visualType/visualScale)以外はここで分岐する必要が無い。
	// (実際の武器モデルを表示する Object3DInstance 版を試したが、Object3D描画パイプラインの
	// 配線でGPUハング(TDR)を起こす未解決の問題がありプリミティブ表示に戻している)
	auto obj = std::make_unique<ArcingProjectile>();
	obj->Initialize(camera_.get(), name, spec, owner, stage_.get(), visualType, visualScale);
	if (thrownWeaponPayload) {
		obj->SetThrownWeaponPayload(std::move(thrownWeaponPayload));
	}
	flyingObjects_.push_back(std::move(obj));
}

void GameScene::UpdateFlyingObjects(float dt) {
	for (auto& obj : flyingObjects_) {
		obj->Update(dt);
		if (obj->IsDead()) {
			continue; // 寿命切れ/地形衝突で既に消えている。命中判定を取る意味がない
		}
		// 発射者自身には ArcingProjectile::TryHitCharacter 内で当たらないようになっている。
		obj->TryHitCharacter(*player_);
		obj->TryHitCharacter(*dummy_);
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
			pickup->Initialize(camera_.get(), obj->GetPosition(), std::move(droppedWeapon));
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
	pickup->Initialize(camera_.get(), spawnPos, CreateRandomWeapon());
	pickups_.push_back(std::move(pickup));
}

void GameScene::Draw() {
	if (stage_) stage_->Draw();
	if (player_) player_->Draw();
	if (dummy_) dummy_->Draw();
	for (auto& pickup : pickups_) {
		pickup->Draw();
	}
	for (auto& obj : flyingObjects_) {
		obj->Draw();
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
		snprintf(hpLine, sizeof(hpLine), "Player HP: %.0f   Dummy HP: %.0f", player_->GetHP(), dummy_->GetHP());
		tr->DrawText(hpLine, { 32.0f, 128.0f }, 0.8f);

		char pointLine[128];
		snprintf(pointLine, sizeof(pointLine), "Points  Player: %d   Dummy: %d", playerPoints_, dummyPoints_);
		tr->DrawText(pointLine, { 32.0f, 160.0f }, 0.8f);

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
