# 09. ポストエフェクト

シーンを一度 RenderTexture に描き、そこへフィルタを順番にかけてから画面に出す仕組み。ピンポン RenderTexture でマルチパスを回すので、複数のフィルタを重ねられる。

---

## セットアップ

```cpp
#include "PostEffect.h"
#include "WindowsApplication.h"

// メンバ: std::unique_ptr<PostEffect> postEffect_;

postEffect_ = std::make_unique<PostEffect>();
postEffect_->Initialize(dxCore_.get(), srvManager_.get(),
                        WindowsApplication::kClientWidth,
                        WindowsApplication::kClientHeight);
```

終了時は `postEffect_->Finalize()`。

---

## 毎フレームの流れ

```cpp
void MyApp::Draw() {
    auto* cmd = dxCore_->GetCommandList();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        dxCore_->GetDsvHeap()->GetCPUDescriptorHandleForHeapStart();

    // 1. シーン用 RenderTexture へ切り替え（クリアもされる）
    postEffect_->BeginSceneRender(cmd, &dsv);
    srvManager_->PreDraw();
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    scene_->Draw();

    postEffect_->EndSceneRender(cmd);

    // 2. スワップチェーンへ切り替え
    dxCore_->BeginDraw();
    srvManager_->PreDraw();

    // Outline 系フィルタが射影行列を必要とする
    if (Camera* cam = object3DManager_->GetDefaultCamera()) {
        postEffect_->SetProjectionMatrix(cam->GetProjectionMatrix());
    }

    // 3. 有効なフィルタを順に適用して出力
    postEffect_->Draw(cmd);                      // スワップチェーンへ
    // postEffect_->Draw(cmd, viewportRT);       // ImGui ビューポートへ出す場合

#ifdef _DEBUG
    ImGuiManager::Instance().EndFrame();
#endif

    dxCore_->EndDraw();
}
```

`Draw()` がピンポン RT を使って有効なフィルタだけを順に適用する。パスの管理は不要。

---

## フィルタ一覧

13種がメンバとして直接生えている。

| メンバ | 効果 |
|---|---|
| `grayscale` | グレースケール |
| `sepia` | セピア |
| `colorInvert` | 色反転 |
| `vignette` | 周辺減光 |
| `gaussian` | ガウスぼかし |
| `smoothing` | 平滑化 |
| `radialBlur` | 放射状ブラー（集中線） |
| `precisionBlur` | 精密射撃用ブラー |
| `dissolve` | ディゾルブ（マスクで溶かす） |
| `outlineDepth` | 深度ベースのアウトライン |
| `outlineNormal` | 法線ベースのアウトライン |
| `maskedGrayscale` | ID マスクで指定オブジェクトだけ色を残す |
| `distortion` | 画面歪み |

### ON / OFF と調整

```cpp
postEffect_->vignette->SetEnabled(true);
postEffect_->vignette->UpdateConstantBuffer();   // パラメータを変えたら呼ぶ

bool on = postEffect_->vignette->IsEnabled();
postEffect_->vignette->ResetParams();
```

パラメータは各フィルタクラスの public メンバ。ImGui の PostEffect パネルで触りながら値を決め、コードに落とすのが早い。

### 適用順を変える

```cpp
postEffect_->SetEffectOrder({ "gaussian", "vignette", "grayscale" });
```

指定した順が先頭に来て、残りは元の順で続く。**ぼかし → 色調 の順にするか逆かで見た目が大きく変わる。**

### 一括操作

```cpp
postEffect_->ResetEffects();              // 全 OFF ＋ パラメータ初期化
postEffect_->ApplyDamageEffect(0.7f);     // 被弾演出（比率を渡す）
```

---

## ImGui から触る

`ImGuiManager` の `getPostEffect` フックを配線すると、PostEffect パネルが有効になる。

```cpp
EditorHostHooks hooks{};
hooks.getPostEffect = []() -> PostEffect* { return MyApp::GetPostEffect(); };
ImGuiManager::SetHostHooks(hooks);
```

未配線だとパネルは空のまま表示される。

---

## 特殊なパス

通常の描画に加えて、専用の RenderTexture へ書き込むパスがある。使わなければ気にしなくてよい。

### ID パス（`maskedGrayscale` 用）

「選択したオブジェクトだけ色を残して他はグレー」のような表現に使う。

```cpp
postEffect_->BeginIdPass(cmd);
// idMaskRT へ対象オブジェクトを描く（Scene::RunIdPass が面倒を見る）
scene_->RunIdPass(cmd);
postEffect_->EndIdPass(cmd);
```

ハイライト対象は `Scene::AddHighlight(entity)` / `RemoveHighlight` / `ClearHighlights` で管理する。

> 対象が無いフレームでも `BeginIdPass` / `EndIdPass` は呼ぶ。RT を SRV 可能な状態に保つため。

### Distortion パス

`useDistortion` を持つエフェクトのプリミティブが歪み情報を書き込む。

```cpp
const bool active = EffectManager::GetInstance()->HasActiveDistortionSource();
postEffect_->distortion->SetEnabled(active);
if (active) {
    postEffect_->BeginDistortionPass(cmd);
    EffectManager::GetInstance()->DrawDistortionPass();
    postEffect_->EndDistortionPass(cmd);
}
```

歪み源が無いフレームはパス全体をスキップして GPU を節約できる。

---

## 注意

- **`BeginSceneRender` と `EndSceneRender` で必ず挟む。** 挟まずに描くとフィルタがかからない
- **RT を切り替えたら `srvManager_->PreDraw()` を呼び直す。** 忘れるとテクスチャが化ける
- `SetProjectionMatrix` を毎フレーム呼ばないと Outline 系が正しく出ない
- フィルタのパラメータを変えたら `UpdateConstantBuffer()` を呼ぶ。呼ばないと反映されない
- 最小構成（`Sample`）は PostEffect を使っていない。使う場合は `Draw()` の組み立てを上記の形に変える必要がある

---

## 関連

- 描画の全体像 → [02_Rendering.md](02_Rendering.md)
- 歪みエフェクトの作り方 → [07_Effects.md](07_Effects.md)
