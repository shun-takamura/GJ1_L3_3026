# 10. エディタと拡張

`Debug` 構成でのみ ImGui のドッキングエディタが立ち上がる。`Release` では `#ifdef _DEBUG` で丸ごと除外される。

---

## 標準ウィンドウ

| ウィンドウ | 内容 |
|---|---|
| **Scene** | ビューポート。デバッグカメラ操作（左ドラッグ=旋回 / 中ドラッグ=平行移動 / ホイール=ズーム）、Play/Pause、アセットのドロップ受付 |
| **Hierarchy** | シーン内エンティティの一覧。グループごとに色分け。目玉トグルで表示切替、`x` で削除 |
| **Inspector** | 選択中エンティティの編集。名前 / 型 / グループ / Visible / Collider ＋ 各エンティティ固有の項目 |
| **SceneEditor** | アセットブラウザ。Models / Sprites / Materials / Animations / Animated / Primitives / Effects。シーン JSON の保存・読込 |
| **Effect Editor / Effect Hierarchy / Effect Components** | エフェクトのオーサリング（[07_Effects.md](07_Effects.md)） |
| **Camera / Light / PostEffect / Particle** | 各種調整 |
| **TimeControler** | タイムスケールとシーンのシーク |
| **Highlights** | ID パスでハイライトする対象の管理 |
| **Pepper** | 自作プロファイラの区間別計測 |
| **FPS / Log / WebCam Devices / QR Code** | その他 |

`ImGuiMenu` から表示/非表示を切り替えられる。レイアウトは `imgui.ini` に保存される。

---

## ホストフック

エンジンはアプリ側の具象クラスを名指ししない。アプリの実体に触る必要がある箇所は**関数ポインタで注入する**。

```cpp
#include "ImGuiManager.h"

EditorHostHooks hooks{};

// --- シーンとアプリの実体 ---
hooks.getActiveScene     = []() -> Scene* { return MyGame::CurrentScene(); };
hooks.getActiveSceneName = []() -> const char* { return MyGame::CurrentSceneName(); };
hooks.getPostEffect      = []() -> PostEffect* { return MyApp::GetPostEffect(); };
hooks.getFramework       = []() -> Framework* { return MyApp::GetInstance(); };

// --- エンティティのグループ（Hierarchy のグルーピング / Inspector の切替）---
// エンジンはグループを「ただの整数」としか見ない。意味づけはアプリが決める
hooks.getEntityGroupCount = []() { return static_cast<int>(Tag::Count); };
hooks.getEntityGroup      = [](IImGuiEditable* e) { return static_cast<int>(MyGame::TagOf(e)); };
hooks.getEntityGroupName  = [](int g) -> const char* { return MyGame::TagName(g); };
hooks.getEntityGroupColor = [](int g, float& r, float& gg, float& b, float& a) {
    MyGame::TagColor(g, r, gg, b, a);
};
hooks.setEntityGroup      = [](IImGuiEditable* e, int g) { MyGame::SetTag(e, g); };

// --- Inspector が編集するコライダーの実体 ---
// アプリが独自のコンポーネント表に持っている場合に配線する。
// 未配線なら CollisionSystem のサイドテーブルが使われる
hooks.getCollider = [](IImGuiEditable* e) -> Collider* {
    return &MyGame::ComponentsOf(e).collider;
};

ImGuiManager::SetHostHooks(hooks);
```

**すべて未配線でも動く**。グループ未配線なら1グループ `All` にまとまり、Inspector のグループコンボは非表示になる。

### エンティティの生成・破棄フック

```cpp
IImGuiEditable::SetHooks(
    [](IImGuiEditable* e) {
        ImGuiManager::Instance().Register(e);
        CollisionSystem::GetInstance()->Register(e);
        MyGame::ComponentsOf(e);          // 自前コンポーネントのエントリ確保など
    },
    [](IImGuiEditable* e) {
        CollisionSystem::GetInstance()->Unregister(e);
        ImGuiManager::Instance().Unregister(e);
        MyGame::RemoveComponents(e);
    });
```

**これを忘れると Hierarchy にも Inspector にも何も出ない。**

---

## エディタを拡張する

### ウィンドウを追加する

`ImGuiManager::Initialize()`（＝`Framework::Initialize()`）の**後**に呼ぶ。

```cpp
auto& imgui = ImGuiManager::Instance();

// 自前のウィンドウクラス
imgui.AddWindow(std::make_unique<MyDebugWindow>(&imgui));

// 描画関数だけの簡易版
imgui.AddCallbackWindow("Wave Editor", []() {
    ImGui::Text("敵の配置");
    if (ImGui::Button("Spawn")) { MyGame::SpawnWave(); }
});
```

自前ウィンドウは `IImGuiWindow` を継承して `OnDraw()` を実装する。

```cpp
class MyDebugWindow : public IImGuiWindow {
public:
    explicit MyDebugWindow(ImGuiManager* m) : IImGuiWindow("My Debug"), manager_(m) {}
protected:
    void OnDraw() override {
        ImGui::Text("Hello");
    }
private:
    ImGuiManager* manager_;
};
```

`ImGui::Begin` / `End` は基底が呼ぶので、`OnDraw()` の中身だけ書けばよい。

### Inspector に項目を追加する

選択中エンティティを受け取る描画関数を登録する。**見出し（`CollapsingHeader`）は登録側が自前で出す**。

```cpp
imgui.AddInspectorSection("Game Components", [](IImGuiEditable* selected) {
    if (ImGui::CollapsingHeader("Battle", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& hp = MyGame::ComponentsOf(selected).hp;
        ImGui::DragInt("HP", &hp.current, 1, 0, hp.max);
    }
    if (ImGui::CollapsingHeader("Prefab")) {
        // ...
    }
});
```

エンジンの共通部（名前 / 型 / グループ / Visible / Collider / `OnImGuiInspector()`）の**後**に、登録順で呼ばれる。

### アセットブラウザに項目を追加する

```cpp
imgui.AddAssetBrowserSection("Prefabs", []() {
    ImGui::TextUnformatted("Prefabs:");
    for (const auto& p : MyGame::AllPrefabs()) {
        ImGui::Button(p.name.c_str());
        if (ImGui::BeginDragDropSource()) {
            PrefabDropPayload pld{};
            SafeCopy(pld.prefabName, sizeof(pld.prefabName), p.name);
            ImGui::SetDragDropPayload(PREFAB_DROP_PAYLOAD_TYPE, &pld, sizeof(pld));
            ImGui::Text("Prefab: %s", p.name.c_str());
            ImGui::EndDragDropSource();
        }
    }
});
```

シーン保存/読込の直後、アセット一覧の前に、登録順で描画される。

---

## エンティティを Inspector 対応にする

`IImGuiEditable` を継承すると Hierarchy / Inspector に載る。

```cpp
class MyActor : public IImGuiEditable {
public:
    std::string GetName() const override     { return name_; }
    void SetName(const std::string& n) override { name_ = n; }
    std::string GetTypeName() const override { return "MyActor"; }

    // Inspector に出す自前の編集 UI
    void OnImGuiInspector() override {
#ifdef USE_IMGUI
        ImGui::DragFloat3("Position", &transform_.translate.x, 0.1f);
        ImGui::DragFloat("Speed", &speed_, 0.1f);
#endif
    }

    // ギズモと当たり判定が使う
    Vector3* GetEditableTranslate() override { return &transform_.translate; }
    const Vector3* GetEditableRotate() const override { return &transform_.rotate; }

private:
    std::string name_ = "MyActor";
    Transform transform_;
    float speed_ = 1.0f;
};
```

`GetEditableTranslate()` を返すと **3D 移動ギズモ**が使えるようになる。2D なら `GetEditable2DPosition()` を返すとスプライトギズモになる。

---

## ドラッグ&ドロップのペイロード

`GameEngine/Utility/EditorDropPayload.h` に定義されている。自前のウィンドウでドロップを受けるときに使う。

| 型 | 中身 |
|---|---|
| `MODEL_DROP_PAYLOAD_TYPE` / `ModelDropPayload` | `dirPath` + `filename` |
| `ANIMATED_DROP_PAYLOAD_TYPE` / `AnimatedDropPayload` | 同上 |
| `SPRITE_DROP_PAYLOAD_TYPE` / `SpriteDropPayload` | `texturePath` |
| `MATERIAL_DROP_PAYLOAD_TYPE` / `MaterialDropPayload` | `materialPath` |
| `ANIM_DROP_PAYLOAD_TYPE` / `AnimDropPayload` | `animPath` |
| `PRIMITIVE_DROP_PAYLOAD_TYPE` / `PrimitiveDropPayload` | `primitiveType` |
| `PREFAB_DROP_PAYLOAD_TYPE` / `PrefabDropPayload` | `prefabName` |
| `EFFECT_RES_DROP_PAYLOAD_TYPE` / `EffectResDropPayload` | `effectName` |
| `EFFECT_COMP_DROP_PAYLOAD_TYPE` / `EffectComponentDropPayload` | `kind` + `meshType` |

いずれも固定長 `char` 配列なので、コピーは長さチェック付きで行うこと。

```cpp
if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SPRITE_DROP_PAYLOAD_TYPE)) {
    const auto* p = static_cast<const SpriteDropPayload*>(payload->Data);
    myTexturePath_ = p->texturePath;
}
```

### ビューポートへのドロップを横取りする

`Scene::OnViewportPrefabDrop()` を override すると、プレハブのドロップをシーン側で処理できる（Wave Editor など）。`true` を返すと通常配置は行われない。

---

## 注意

- 追加系 API（`AddWindow` / `AddInspectorSection` / `AddAssetBrowserSection`）は **`Framework::Initialize()` の後**に呼ぶ
- `Release` ではこれらは空実装になる。`#ifdef _DEBUG` で囲んでおくと意図が明確
- `imgui.ini` にレイアウトが保存される。ウィンドウが見当たらないときは `ImGuiMenu` から表示を戻す
- 新しくウィンドウを足しても既存レイアウトには入らない。フローティング状態で出てくるので手でドッキングする

---

## 関連

- 最初の配線 → [01_GettingStarted.md](01_GettingStarted.md)
- 当たり判定のフック → [06_Collision.md](06_Collision.md)
