#include "GameScene.h"

#include "SceneManager.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "ControllerInput.h"
#include "Object3DManager.h"
#include "LightManager.h"
#include "TextRenderer.h"
#include "WindowsApplication.h"
#include "TimeGroup.h"
#include "Physics/CollisionSystem.h"
#include "Primitive/DebugDraw.h"
#include "Primitive/LineRenderer.h"
#include "Log.h"

#include <cstdio>
#include <dinput.h>
#include <Xinput.h>

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
	// 入力 → プレイヤーの意図(左右移動・ジャンプ・しゃがみ・攻撃)への変換
	//
	// ここが「入力デバイス」と「Character の中身」を繋ぐ唯一の場所。
	// Character::Update() はキーボードもゲームパッドも一切知らないので、
	// GameScene が代わりにデバイスの生の状態を読み、意味のある意図(moveX 等)に
	// 変換してから渡している。AI(フェーズ4)を実装するときは、ここでの
	// 「キーボード/パッドを読む」処理の代わりに「AIが行動を決める」処理を書き、
	// 同じ Character::Update() を呼べばよい(Character 側は無改造で済む)。
	//
	// 横視点ゲームなので移動はX軸のみ(左スティックの上下=奥行き成分は使わない)。
	//===================================
	float moveX = 0.0f;
	bool jumpTriggered = false;
	bool crouchHeld = false;
	bool attackTriggered = false;
	if (input_) {
		if (auto* kb = input_->GetKeyboard()) {
			if (kb->PushKey(DIK_A)) moveX -= 1.0f;
			if (kb->PushKey(DIK_D)) moveX += 1.0f;
			jumpTriggered |= kb->TriggerKey(DIK_W);  // 押した瞬間だけ true(押しっぱなしで連続ジャンプしない)
			crouchHeld |= kb->PushKey(DIK_S);        // 押している間ずっと true
			attackTriggered |= kb->TriggerKey(DIK_SPACE);
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
		}
	}

	stage_->Update();

	player_->Update(dt, moveX, jumpTriggered, crouchHeld, attackTriggered);
	dummy_->Update(dt, 0.0f, false, false, false); // 的は入力なしで呼ぶだけ(重力等の物理更新は必要なので Update 自体は呼ぶ)

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
	// 場外・HP0判定 → 仮の得点+その場リセット
	// 本物の「10ポイント先取・次ステージへ自動遷移」といったラウンド進行はフェーズ5の別タスク。
	// ここでは「テストを継続できること」を優先して、即座にリセットするだけにしている。
	//===================================
	CheckKnockoutAndReset(*player_, *dummy_, dummyPoints_, playerSpawn_, "Player");
	CheckKnockoutAndReset(*dummy_, *player_, playerPoints_, enemySpawn_, "Dummy");

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
	if (defender.ReceiveHit(hitbox)) {
		Log(std::string(attackerLabel) + " の攻撃が命中\n");
	}
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

void GameScene::Draw() {
	if (stage_) stage_->Draw();
	if (player_) player_->Draw();
	if (dummy_) dummy_->Draw();

	//===================================
	// デバッグ線の描画。
	// DebugDraw::* は線をキューに積むだけなので、
	// 最後に LineRenderer::Draw() を呼ばないと何も出ない(エンジンの定番の落とし穴)。
	//===================================
	DebugDraw::Grid({ 0.0f, 0.01f, 0.0f }, 20.0f, 1.0f, { 0.4f, 0.4f, 0.5f, 1.0f });

	auto* lr = LineRenderer::GetInstance();
	lr->SetCamera(GetCamera()); // カメラ未設定だと線の描画位置が定まらないので必須
	lr->Draw();

	//===================================
	// 画面左上のデバッグ表示(操作方法・HP・仮の得点)。
	// 本物のUI(フェーズ5)が入るまでの動作確認用。
	//===================================
	auto* tr = TextRenderer::GetInstance();
	if (tr && tr->IsInitialized()) {
		tr->DrawText("A/D : Move   W/A(pad) : Jump   S/Down(pad) : Crouch   Space/X : Punch", { 32.0f, 32.0f }, 0.8f);
		tr->DrawText("ESC / (B) : Title", { 32.0f, 64.0f }, 0.8f);

		char hpLine[128];
		snprintf(hpLine, sizeof(hpLine), "Player HP: %.0f   Dummy HP: %.0f", player_->GetHP(), dummy_->GetHP());
		tr->DrawText(hpLine, { 32.0f, 96.0f }, 0.8f);

		char pointLine[128];
		snprintf(pointLine, sizeof(pointLine), "Points  Player: %d   Dummy: %d", playerPoints_, dummyPoints_);
		tr->DrawText(pointLine, { 32.0f, 128.0f }, 0.8f);

		tr->Flush(); // スプライトと同じタイミング(描画順の最後)で確定させる
	}
}
