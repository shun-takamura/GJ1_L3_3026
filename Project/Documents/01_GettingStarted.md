# 01. はじめに — アプリの組み立て方

ArcanaEngine は静的ライブラリ（`ArcanaEngine.lib`）として提供される。
アプリ側が書くのは **3つのクラス** と **フックの配線** だけで、DirectX12 の初期化・ウィンドウ・入力・各マネージャ・ImGui エディタはすべてエンジンが用意する。

動く実例は `Project/Sample/` にある。迷ったらそこを読むのが早い。

---

## 全体像

```
main.cpp          WinMain。Framework 派生を作って Run() を呼ぶ
  └ MyApp        : Framework      アプリ本体。Draw() の組み立てとフック配線
      └ MySceneRunner : ISceneRunner   シーンの生成・更新・破棄
          └ MyScene   : Scene           ゲームの中身
```

`Framework::Run()` の中身はこうなっている。アプリはこの流れに乗るだけでよい。

```
Initialize()          … Framework::Initialize が全マネージャを初期化し、
                        GetSceneRunner()->Initialize(...) を呼ぶ
while (true) {
    Update()          … Framework::Update が入力更新・ImGui BeginFrame・
                        GetSceneRunner()->Update() を呼ぶ
    if (IsEndRequest()) break;
    Draw()            … アプリが実装する（純粋仮想）
}
Finalize()
```

---

## 最小コード

### main.cpp

```cpp
#include <memory>
#include "MyApp.h"
#include "CrashHandler.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // 未捕捉例外で .dmp とスタックを残す。最初に仕込む
    CrashHandler::Install();

    std::unique_ptr<Framework> app = std::make_unique<MyApp>();
    app->Run();
    return 0;
}
```

### シーン

`Scene` は純粋仮想を4つ持つ。`GetCamera()` は override しておくと、デバッグカメラや各マネージャがそのカメラを基準にしてくれる。

```cpp
// MyScene.h
#pragma once
#include <memory>
#include "Scene.h"
#include "Camera.h"
#include "Primitive/PrimitiveInstance.h"

class MyScene : public Scene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

    Camera* GetCamera() override { return camera_.get(); }

private:
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<PrimitiveInstance> cube_;
};
```

```cpp
// MyScene.cpp
#include "MyScene.h"
#include "DirectXCore.h"
#include "Object3DManager.h"
#include "TimeGroup.h"

void MyScene::Initialize() {
    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 2.0f, -8.0f });
    camera_->SetRotate({ 0.15f, 0.0f, 0.0f });
    camera_->Update();

    // 以降の描画がこのカメラ基準になる
    if (object3DManager_) {
        object3DManager_->SetDefaultCamera(camera_.get());
    }

    cube_ = std::make_unique<PrimitiveInstance>();
    cube_->Initialize(PrimitiveInstance::PrimitiveType::Box, "MyCube");
    cube_->SetCamera(camera_.get());
}

void MyScene::Finalize() {
    cube_.reset();
    camera_.reset();
}

void MyScene::Update() {
    // デバッグカメラが有効ならその行列をシーンカメラへ注入する（Scene 基底の機能）
    UpdateDebugCameraIfActive();
    if (!GetUseDebugCamera()) {
        camera_->Update();
    }

    // 時間停止やスローに追従させるため、生の delta ではなく TimeGroup 付きを使う
    const float dt = GetScaledDeltaTime(TimeGroup::World);
    cube_->Update();
}

void MyScene::Draw() {
    cube_->Draw();
}
```

`spriteManager_` `object3DManager_` `skyboxManager_` `dxCore_` `srvManager_` `input_` `skinningComputeManager_` は `Scene` の protected メンバとして用意されている。実体は Runner が注入する。

### シーンランナー

`Framework` はこのインターフェース越しにしかシーンを触らない。実際のゲームでは複数シーンを切り替える SceneManager がここに来る。

```cpp
// MyApp.h（抜粋）
class MySceneRunner : public ISceneRunner {
public:
    MySceneRunner();
    ~MySceneRunner() override;

    void Initialize(SpriteManager*, Object3DManager*, SkyboxManager*,
                    DirectXCore*, SRVManager*, InputManager*,
                    SkinningComputeManager*) override;
    void Update() override;
    void Finalize() override;

    MyScene* GetScene() const { return scene_.get(); }

private:
    std::unique_ptr<MyScene> scene_;
};
```

```cpp
void MySceneRunner::Initialize(
    SpriteManager* spriteManager, Object3DManager* object3DManager,
    SkyboxManager* skyboxManager, DirectXCore* dxCore, SRVManager* srvManager,
    InputManager* input, SkinningComputeManager* skinningComputeManager)
{
    scene_ = std::make_unique<MyScene>();

    // Scene 基底が要求するマネージャを注入する
    scene_->SetSpriteManager(spriteManager);
    scene_->SetObject3DManager(object3DManager);
    scene_->SetSkyboxManager(skyboxManager);
    scene_->SetDirectXCore(dxCore);
    scene_->SetSRVManager(srvManager);
    scene_->SetInputManager(input);
    scene_->SetSkinningComputeManager(skinningComputeManager);

    scene_->Initialize();
}

void MySceneRunner::Update()   { if (scene_) scene_->Update(); }
void MySceneRunner::Finalize() { if (scene_) { scene_->Finalize(); scene_.reset(); } }
```

> **`Framework` と `ISceneRunner` を多重継承してはいけない。**
> `Update()` と `Finalize()` のシグネチャが完全に一致するため、1つの override が両方を上書きして無限再帰する。必ず別クラスにする。

### アプリ本体

```cpp
void MyApp::Initialize() {
    // ★最初に配線する。以降に作られる全 IImGuiEditable がここを通る
    IImGuiEditable::SetHooks(
        [](IImGuiEditable* e) {
            ImGuiManager::Instance().Register(e);
            CollisionSystem::GetInstance()->Register(e);
        },
        [](IImGuiEditable* e) {
            CollisionSystem::GetInstance()->Unregister(e);
            ImGuiManager::Instance().Unregister(e);
        });

    // Framework::Initialize が GetSceneRunner()->Initialize(...) を呼ぶので、
    // 基底の初期化より先に実体を用意しておく
    runner_ = std::make_unique<MySceneRunner>();

    Framework::Initialize();
}

void MyApp::Draw() {
    auto* cmd = dxCore_->GetCommandList();

    dxCore_->BeginDraw();
    const float clearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
    dxCore_->ClearRenderTarget(clearColor);   // クリア＋深度クリア＋DSV バインド
    srvManager_->PreDraw();                   // ディスクリプタヒープを積む

    if (runner_ && runner_->GetScene()) {
        runner_->GetScene()->Draw();
    }

#ifdef _DEBUG
    ImGuiManager::Instance().EndFrame();
#endif

    dxCore_->EndDraw();
    dxCore_->TickIntermediateResources();
    dxCore_->TickPendingCallbacks();
}
```

---

## 必ず踏む落とし穴

### `IImGuiEditable::SetHooks()` を忘れない

これを配線しないと、生成したエンティティが **Hierarchy にも Inspector にも当たり判定にも登録されない**。「オブジェクトは描画されているのにエディタに出てこない」場合はまずここを疑う。

### `Framework` 派生のコンストラクタ／デストラクタは `.cpp` に置く

`Framework` は前方宣言だけの型を `unique_ptr` で持っている。派生クラスで `= default` をヘッダに書くと、そのクラスを **構築する／破棄する翻訳単位すべて** で完全型が要求され、`can't delete an incomplete type` になる。

```cpp
// MyApp.h
class MyApp : public Framework {
public:
    MyApp();            // 宣言だけ
    ~MyApp() override;  // 宣言だけ
    ...
};
```

```cpp
// MyApp.cpp
MyApp::MyApp()  = default;
MyApp::~MyApp() = default;
```

コンストラクタ側も対象になるのは、メンバ構築が例外を投げたときの巻き戻しコードが `unique_ptr` の破棄を要求するため。

### 作業ディレクトリは `Project/`

エンジンは `Resources/` を `Project/` からの相対パスで読む。`Sample.vcxproj` には `LocalDebuggerWorkingDirectory` が設定済みなので VS からの実行は問題ないが、**ビルド出力フォルダの exe を直接ダブルクリックすると `Resources/` が見つからず落ちる**。配布物として動かす場合は zip を展開したフォルダから実行すること。

---

## 次に読むもの

- モデルやスプライトを出す → [02_Rendering.md](02_Rendering.md)
- 自分の素材を追加する → [03_Assets.md](03_Assets.md)
- エディタを拡張する → [10_Editor.md](10_Editor.md)
- うまくいかない → [12_Pitfalls.md](12_Pitfalls.md)
