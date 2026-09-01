# 02. 描画 — モデル / アニメーション / スプライト / プリミティブ

すべての描画物は **生成 → `Initialize` → `SetCamera` → 毎フレーム `Update` → `Draw`** という同じ流れになっている。破棄は `unique_ptr` に任せる（`delete` は書かない）。

`Object3DInstance` / `AnimatedObject3DInstance` / `SpriteInstance` / `PrimitiveInstance` はいずれも `IImGuiEditable` を継承しているので、生成した時点で **自動的に Hierarchy と Inspector に出る**（`IImGuiEditable::SetHooks` を配線してあれば）。

---

## 通常モデル（Object3DInstance）

`.mesh` を描画する。元データは `Assets/Models/**` の `.obj` / `.gltf` で、ビルド時に変換される（[03_Assets.md](03_Assets.md)）。

```cpp
#include "Object3DInstance.h"
#include "ModelManager.h"

// メンバ: std::unique_ptr<Object3DInstance> obj_;

// --- Initialize ---
obj_ = std::make_unique<Object3DInstance>();
obj_->Initialize(object3DManager_, dxCore_,
                 "Resources/Models/Enemy",   // ディレクトリ
                 "enemy.mesh",               // ファイル名
                 "Enemy01");                 // Hierarchy 上の表示名（省略可）
obj_->SetCamera(GetCamera());
obj_->SetTranslate({ 0.0f, 0.0f, 10.0f });
obj_->SetRotate({ 0.0f, 3.14f, 0.0f });      // ラジアン
obj_->SetScale({ 1.0f, 1.0f, 1.0f });

// --- Update ---
obj_->Update();

// --- Draw ---
obj_->Draw(dxCore_);

// --- 破棄 ---
obj_.reset();   // デストラクタで Unregister まで走る
```

### よく使う設定

```cpp
obj_->SetTexture("Resources/Textures/enemy_red.dds");  // テクスチャ差し替え
obj_->SetMaterialColor({ 1.0f, 0.3f, 0.3f, 1.0f });    // 乗算カラー
obj_->SetUseEnvironmentMap(true);                       // 環境マップ反射
obj_->SetEnvironmentCoefficient(0.4f);
obj_->SetShadingModel(1);                               // 0=BlinnPhong, 1=PBR
obj_->SetMetallic(0.9f);                                // PBR 時のみ有効
obj_->SetRoughness(0.2f);
```

### 行列を直接与える

ボーン追従など、Transform では表せない配置をしたいとき。

```cpp
obj_->SetWorldMatrixOverride(someMatrix);
obj_->ClearWorldMatrixOverride();   // 解除して Transform に戻す
```

### 影を落とす／ID パスに出す

シャドウパスとハイライト用 ID パスは通常の `Draw` とは別に呼ぶ。`Scene` の動的オブジェクトを使っている場合は `Scene::DrawShadowCasters()` が面倒を見る。

```cpp
obj_->DrawShadowPass(dxCore_);
obj_->DrawIdPass(dxCore_);
```

---

## アニメーションモデル（AnimatedObject3DInstance）

スキニング付きモデル。**`AnimatedModelInstance`（データ）と `AnimatedObject3DInstance`（描画実体）の2つ**が要る点に注意。

```cpp
#include "AnimatedModelInstance.h"
#include "AnimatedObject3DInstance.h"
#include "ModelManager.h"

// メンバ:
//   std::unique_ptr<AnimatedModelInstance>    model_;
//   std::unique_ptr<AnimatedObject3DInstance> chara_;

// --- Initialize ---
model_ = std::make_unique<AnimatedModelInstance>();
model_->Initialize(ModelManager::GetInstance()->GetModelCore(),
                   "Resources/Models/Player", "player.mesh");

chara_ = std::make_unique<AnimatedObject3DInstance>();
chara_->Initialize(object3DManager_, skinningComputeManager_, dxCore_, srvManager_,
                   model_.get(), "Player");
chara_->SetSourcePath("Resources/Models/Player", "player.mesh");
chara_->SetCamera(GetCamera());

// --- Update（deltaTime を渡す） ---
chara_->Update(GetScaledDeltaTime(TimeGroup::Player));

// --- Draw（スキニングの Dispatch が先） ---
chara_->DispatchSkinning(dxCore_);   // srvManager_->PreDraw() の後で呼ぶ
chara_->Draw(dxCore_);
```

> `model_` は `chara_` より長生きさせること。`chara_` は `model_` を生ポインタで参照している。

### アニメーション再生

```cpp
chara_->PlayAnimation("Resources/Models/Player/Walk/walk.anim", 0.2f);  // 0.2秒でブレンド
chara_->SetLoop(true);
chara_->SetPlaybackSpeed(1.5f);

chara_->Play();
chara_->Pause();
chara_->Stop();

chara_->SetAnimationTime(0.0f);
float t = chara_->GetAnimationTime();
bool playing = chara_->IsPlaying();
bool fading  = chara_->IsFading();      // ブレンド中か

chara_->SetDefaultFadeTime(0.15f);      // 以降の既定ブレンド時間
```

### ボーンに物を持たせる

武器やエフェクトを手のボーンに追従させる。

```cpp
if (chara_->HasSkeleton()) {
    // ソケット行列（スケール除去済み・正規直交化済み）
    Matrix4x4 hand = chara_->GetJointSocketMatrix("mixamorig:RightHand");
    weapon_->SetWorldMatrixOverride(hand);
}
```

`GetJointWorldMatrix()` はスケールを含む生の行列。装備品の追従には `GetJointSocketMatrix()` を使う方が安定する。

### スケルトンの可視化

```cpp
chara_->DrawSkeletonDebug(dxCore_);   // Debug 用。ボーンを線で描く
```

---

## スプライト（SpriteInstance）

2D 描画。座標は**クライアント座標（ピクセル・左上原点）**。

```cpp
#include "SpriteInstance.h"

// メンバ: std::unique_ptr<SpriteInstance> hpBar_;

// --- Initialize ---
hpBar_ = std::make_unique<SpriteInstance>();
hpBar_->Initialize(spriteManager_, "Resources/Textures/hp_gauge_fill.dds", "HPBar");
hpBar_->SetPosition({ 40.0f, 30.0f });
hpBar_->SetSize({ 320.0f, 24.0f });
hpBar_->SetAnchorPoint({ 0.0f, 0.0f });    // 0,0=左上 / 0.5,0.5=中心

// --- Update / Draw ---
hpBar_->Update();
hpBar_->Draw();
```

### よく使う設定

```cpp
hpBar_->SetColor({ 1.0f, 1.0f, 1.0f, 0.8f });   // 乗算カラー（アルファで透過）
hpBar_->SetRotation(0.3f);                       // ラジアン
hpBar_->SetIsFlipX(true);
hpBar_->SetIsFlipY(false);
hpBar_->SetTexture("Resources/Textures/hp_gauge_back.dds");

// テクスチャの一部だけ切り出す（アトラス・ゲージの増減）
hpBar_->SetTextureLeftTop({ 0.0f, 0.0f });
hpBar_->SetTextureSize({ 128.0f, 32.0f });
```

ゲージを減らすなら `SetSize` と `SetTextureSize` を同じ比率で縮める。

---

## プリミティブ（PrimitiveInstance）

モデルを用意せずに形を出せる。試作やエフェクトで多用する。

```cpp
#include "Primitive/PrimitiveInstance.h"

cube_ = std::make_unique<PrimitiveInstance>();
cube_->Initialize(PrimitiveInstance::PrimitiveType::Box, "Cube");
cube_->SetCamera(GetCamera());
cube_->SetTranslate({ 0, 1, 0 });
cube_->SetScale({ 2, 2, 2 });
cube_->SetTexture("Resources/Textures/stripe.dds");

cube_->Update();
cube_->Draw();      // 内部で PrimitivePipeline::PreDraw まで面倒を見る
```

用意されている形状は `Plane` / `Box` / `Sphere` / `Ring` / `Cylinder` / `Helix` / `Hemisphere` など。既定テクスチャは `Resources/Textures/white1x1.dds`。

---

## カメラ

```cpp
#include "Camera.h"

camera_ = std::make_unique<Camera>();
camera_->SetTranslate({ 0.0f, 5.0f, -20.0f });
camera_->SetRotate({ 0.2f, 0.0f, 0.0f });     // ラジアン（X=ピッチ, Y=ヨー, Z=ロール）
camera_->SetFovY(0.45f);
camera_->SetNearClip(0.1f);
camera_->SetFarClip(1000.0f);
camera_->Update();                             // 毎フレーム必要
```

`Scene::GetCamera()` を override して返すと、デバッグカメラ・当たり判定のデバッグ描画・エフェクトなどがこのカメラを基準にする。各マネージャに使わせるには `object3DManager_->SetDefaultCamera(camera_.get())` も呼ぶ。

### 画面シェイク

```cpp
camera_->Shake(0.5f, 0.2f);   // 強さ, 継続秒
camera_->StopShake();
```

### デバッグカメラ

`Scene` の機能として組み込み済み。ImGui の Camera パネルの `Use Debug Camera` で切り替わる。

```cpp
void MyScene::Update() {
    UpdateDebugCameraIfActive();          // 有効なら行列をシーンカメラへ注入
    if (!GetUseDebugCamera()) {
        camera_->Update();                // 通常時だけ自前更新
    }
}
```

操作は Scene ビューポート上で 左ドラッグ=旋回 / 中ドラッグ=平行移動 / ホイール=ズーム。

---

## ライト

`LightManager` はシングルトン。`Initialize` は `Framework` が済ませてある。

```cpp
#include "LightManager.h"

auto* lm = LightManager::GetInstance();

// 平行光源（1個。シャドウマップもこれを基準にする）
lm->SetDirectionalLightDirection({ -0.3f, -1.0f, 0.4f });
lm->SetDirectionalLightColor({ 1.0f, 0.95f, 0.9f, 1.0f });
lm->SetDirectionalLightIntensity(1.2f);

// ポイントライト（スロットを確保して使う）
uint32_t slot = lm->AcquirePointLight();
lm->SetPointLightPosition(slot, { 0, 3, 0 });
lm->SetPointLightColor(slot, { 1, 0.5f, 0.2f, 1 });
lm->SetPointLightIntensity(slot, 4.0f);
lm->SetPointLightRadius(slot, 12.0f);
lm->SetPointLightDecay(slot, 1.0f);
// 使い終わったら返す
lm->ReleasePointLight(slot);
```

スポットライトも `AcquireSpotLight()` / `SetSpotLight*` / `ReleaseSpotLight()` で同じ形。

> **平行光源の既定の向きは 0 ベクトルになっていることがある。** 何も設定しないと真っ暗になるので、シーンの `Initialize` で必ず向きを入れる。

### 描画時のバインド

Object3D を描く前に一度呼ぶ。

```cpp
object3DManager_->DrawSetting();
LightManager::GetInstance()->BindLights(dxCore_->GetCommandList());
```

---

## Scene::Draw() の書き方と描画順

順序を間違えると「描いたはずのものが消える」。基本はこの順。

```cpp
void MyScene::Draw() {
    auto* cmd = dxCore_->GetCommandList();

    // 1. Skybox — 最初。深度を書かないので read-only DSV でよい
    auto rtv = /* 描画先 RTV */;
    auto readOnlyDsv = dxCore_->GetReadOnlyDsvHandle();
    cmd->OMSetRenderTargets(1, &rtv, false, &readOnlyDsv);
    skyboxManager_->DrawSetting();
    skybox_->Draw(dxCore_);

    // 2. Object3D 以降 — 通常 DSV に戻して深度書き込みを有効化
    auto dsv = dxCore_->GetDsvHeap()->GetCPUDescriptorHandleForHeapStart();
    cmd->OMSetRenderTargets(1, &rtv, false, &dsv);

    object3DManager_->DrawSetting();
    LightManager::GetInstance()->BindLights(cmd);
    for (auto& o : objects_) o->Draw(dxCore_);

    // 3. アニメーションモデル
    for (auto& a : animated_) a->Draw(dxCore_);

    // 4. プリミティブ（半透明はここ）
    cube_->Draw();

    // 5. エフェクト
    DrawGlobalEffects();

    // 6. スプライト・テキスト — 最後（2D は深度を無視する）
    hpBar_->Draw();
}
```

`Scene` を使っている場合、動的オブジェクトは `DrawDynamicObjects()` / `DrawDynamicAnimated()` / `DrawDynamicPrimitives()` / `DrawDynamicSprites()` でまとめて描ける。

### `srvManager_->PreDraw()` を呼ぶタイミング

**レンダーターゲットを切り替えたら必ず呼び直す。** ディスクリプタヒープの再バインドを行うため、これを忘れると別の RT へ描いた後の描画でテクスチャが化ける。

---

## 破棄について

- 生ポインタの `delete` は書かない。所有は `std::unique_ptr`、参照だけなら生ポインタ
- `Scene::Finalize()` で `reset()` するか、メンバのまま自然に破棄させる
- GPU がまだ使用中のリソースを即破棄するとエラーになる。`Scene` の動的オブジェクトは `deferredDeletes_` に退避されて次フレームに破棄される。自前で管理する場合も、描画中の破棄は避ける

---

## 関連

- テキスト描画 → [11_Utilities.md](11_Utilities.md)
- ポストエフェクト → [09_PostEffect.md](09_PostEffect.md)
- エフェクト → [07_Effects.md](07_Effects.md)
