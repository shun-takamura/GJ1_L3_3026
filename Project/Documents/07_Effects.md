# 07. エフェクト と Effect Editor

エフェクトは **JSON（`EffectDef`）で定義し、ゲームからは名前で再生する**。定義は ImGui の Effect Editor で作る。C++ を書かずにエフェクトを増やせる。

```
Resources/Json/Effects/Explosion_00.json      ← 定義（Effect Editor が読み書きする）
        │
        ▼
EffectManager::GetInstance()->Play("Explosion_00", pos);
```

1ファイル = 1エフェクト。エフェクトは **Primitive / Particle / Light / Sound** の4種のコンポーネントを任意個持つ。

---

## コードからの再生

### 初期化（アプリ起動時に一度）

```cpp
#include "GPUParticleManager.h"
#include "Effect/EffectManager.h"

gpuParticleManager_ = std::make_unique<GPUParticleManager>();
gpuParticleManager_->Initialize(dxCore_.get(), srvManager_.get());
gpuParticleManager_->CreateGroup("spark", "Resources/Textures/circle.dds");

EffectManager::GetInstance()->Initialize(gpuParticleManager_.get());
EffectManager::GetInstance()->LoadAllDefsInDirectory("Resources/Json/Effects");
```

### 再生

```cpp
auto* em = EffectManager::GetInstance();
em->SetCamera(GetCamera());   // ビルボードと 3D サウンドの基準

EffectHandle h = em->Play("Explosion_00", { 0.0f, 1.0f, 5.0f });

// 追従させる（弾の trail など）
em->SetPosition(h, bulletPos);
em->SetRotation(h, bulletRotate);

if (em->IsAlive(h)) { /* まだ再生中 */ }
em->Stop(h);   // loop = true のエフェクトはこれで止める
```

### 更新と描画

`Scene` を使っているなら `UpdateGlobalEffects()` / `DrawGlobalEffects()` を呼ぶだけでよい。

```cpp
void MyScene::Update() {
    // deltaTime は unscaled な実 delta を渡す。
    // 各コンポーネントの TimeGroup で内部スケールされる
    UpdateGlobalEffects(GetCamera(), realDeltaTime);
}

void MyScene::Draw() {
    DrawGlobalEffects();
}
```

### 終了処理

GPU を止める前に解放する。

```cpp
EffectManager::GetInstance()->Finalize();
gpuParticleManager_->Finalize();
gpuParticleManager_.reset();
```

### 定義を直接組み立てて再生する

JSON を経由せずコードから作ることもできる。

```cpp
EffectDef def;
def.name = "Runtime";
def.totalDuration = 0.5f;
EffectPrimitiveComponent p;
p.meshType = 2;                     // Sphere
p.startScale = { 0.1f, 0.1f, 0.1f };
p.endScale   = { 3.0f, 3.0f, 3.0f };
def.primitives.push_back(p);

em->PlayWithDef(def, pos);
```

---

## Effect Editor（Debug ビルド）

3つのウィンドウが連携する。

| ウィンドウ | 役割 |
|---|---|
| **Effect Editor** | プレビュー表示、再生制御、選択中コンポーネントの詳細編集 |
| **Effect Hierarchy** | 編集中エフェクトのコンポーネント一覧。ここで選択する |
| **Effect Components** | 追加できるコンポーネントのパレット。Hierarchy へドラッグ&ドロップして追加 |

**Inspector にコンポーネントの全パラメータが出る**。Effect Hierarchy で選択 → Inspector で調整、という流れになる。

編集内容は**再生し直さずに即反映される**（ライブ反映）。プレビューはシーンのタイムスケールから独立した専用タイムラインで動くので、ゲームを止めていてもエフェクトだけ再生できる。

`Resources/Json/Effects/` は監視されていて、外部でファイルを足すと自動でリロードされる。

---

## 共通の考え方

すべてのコンポーネントが持つ項目。

| 項目 | 意味 |
|---|---|
| `displayName` | Hierarchy 上の表示名。空なら種別名 |
| `offset` | エフェクト中心からのローカルオフセット |
| `startTime` | エフェクト再生開始からの**絶対秒**。これで時間差の演出を作る |
| `timeGroup` | `0=World / 1=Player / 2=UI / 3=Effect`。進行速度の倍率をどのグループに従わせるか |

`totalDuration` はエフェクト全体の尺。`loop = true` にすると終了時に自動で最初から再生し直す（弾の trail 等）。止めるときは `Stop(handle)`。

> **時間停止中も動かしたい演出は `timeGroup` を `Effect` にする。** 必殺技でワールドを止めても、エフェクトだけは進む、といった表現ができる。

---

## Primitive コンポーネント

形のあるメッシュを出す。爆発の球、衝撃波のリング、ビーム、雷など。

### 形状（`meshType`）

`0=Plane / 1=Box / 2=Sphere / 3=Ring / 4=Cylinder / 5=Helix / 6=Beam / 7=Lightning / 8=Hemisphere / 9=Frame`

形状ごとに専用のジオメトリパラメータがある（`ringParams` `cylinderParams` `helixParams` `beamParams` `lightningParams` `frameParams`）。

### 時間とアニメーション

| 項目 | 内容 |
|---|---|
| `lifetime` | このコンポーネントの表示時間 |
| `startScale` → `endScale` | サイズ補間 |
| `startColor` → `endColor` | 色補間。アルファを 1→0 にするとフェードアウト |
| `scaleCurve` | スケール補間のイージング。`t` を再マップしてから Lerp する |
| `usePositionAnim` / `startPos` / `endPos` / `posCurve` | 位置を動かす。有効時は `offset` の代わりに使われる |

### 回転

| 項目 | 内容 |
|---|---|
| `rotate` | 基準回転（ラジアン） |
| `randomRotateOnSpawn` / `randomRotateRange` | 出現時に各軸 ±範囲でランダムな初期姿勢 |
| `rotateSpeed` | 角速度ベクトル（rad/s）。生存中ずっと回り続ける |

内部はクオータニオン合成なのでジンバルロックしない。

### 見た目

| 項目 | 内容 |
|---|---|
| `texturePath` | 空なら `white1x1` |
| `blendMode` | `0=None / 1=Normal / 2=Add / 3=Subtract / 4=Multiply / 5=Screen`。既定は Add |
| `billboardMode` | `Full`（常にカメラを向く）/ `YAxis` / `None` |
| `depthWrite` | 既定 false。半透明の重なりを自然にするため |
| `cullBackface` | 既定 false（両面描画） |
| `alphaReference` | これ未満のアルファを discard |
| `samplerMode` | `0=WrapAll / 1=WrapU+ClampV / 2=ClampAll`。Ring や Cylinder は 1 が既定 |
| `viewAngleFadePower` | 視線と面の角度でフェード。雷やレーザーの斜め面を消す。`2〜4` 推奨、`0` で無効 |

### UV とディゾルブ

```
uvAutoScroll / uvScrollSpeed / uvOffset    UV スクロール（ビーム・流れる模様）
hueShiftEnable / hueShiftSpeed             色相を時間で回す（1.0 = 毎秒1周）
```

Primitive にもディゾルブ（マスクテクスチャで溶けるように消す）が用意されている。

---

## Particle コンポーネント

GPU パーティクル。粒の集合。

### 発生

| 項目 | 内容 |
|---|---|
| `burstCount` | 発生数 |
| `duration` | 発生を受け付ける期間（秒） |
| `particleLifeTime` | 粒1つあたりの寿命 |
| `emitShape` | `0=Sphere`（球状に散らす）/ `1=Ring`（円周上） |
| `emitRadius` | 散らばり半径。Ring では円の半径 |
| `ringNormal` / `ringThickness` | Ring 用。平面の法線と帯の太さ |

### 初速（`velocityMode`）

| 値 | 挙動 |
|---|---|
| `0` | 全方向ランダム |
| `1` | 方向固定（`velocityDir`） |
| `2` | 放射（中心から外へ） |
| `3` | 接線（公転の初速） |

`velocitySpeed` が大きさ、`velocityJitter` がゆらぎ量。

### 色

| 項目 | 内容 |
|---|---|
| `colorMode` | `0=Random`（start/end 無視）/ `1=Fixed` |
| `startColor` → `endColor` | Fixed 時の2色補間 |
| `colorKeys` | 2個以上入れると多色グラデーションになる |
| `hueShiftEnable` / `hueShiftSpeed` / `hueShiftRandomPhase` | 色相を回す。位相をばらすと虹色が滑らかに散る |

### サイズ

```
scaleMin / scaleMax        発生時のランダムサイズ範囲（幅, 高さ）
uniformScale               true なら 幅=高さ を強制
startScale → endScale      寿命に沿った倍率（Size over Lifetime）。1.0/1.0 で変化なし
```

### 動き

| 項目 | 内容 |
|---|---|
| `orbitEnabled` / `orbitSpinSpeed` | リング法線軸まわりに帯上を流れる |
| `orbitTumbleSpeed` / `orbitTumbleAxis` | 帯自体を回す |
| `convergeEnable` / `convergeCurve` | **収束**。物理を使わず spawn 位置から中心へ寄せる。カーブで進み具合を制御（0=spawn, 1=中心） |
| `randomRotateOnSpawn` / `rotateSpeed` | 板を3D回転させる（破片のタンブル）。**ビルボードが `None` のときだけ有効** |

### ディゾルブ

粒ごとに自分の寿命比率で溶ける。

```
useDissolve / dissolveMaskPath
dissolveInEnable  / dissolveInEnd     出現: [0, inEnd] で現れる
dissolveOutEnable / dissolveOutStart  消滅: [outStart, 1] で消える
dissolveEdgeEnable / dissolveEdgeColor / dissolveEdgeWidth   燃えるエッジ
```

### ブレンドの注意

`blendMode` の既定は `Add`。**加算では黒い粒子が映らない**ので、黒煙などは `Normal` にする。

---

## Light コンポーネント

爆発の閃光など。`PointLight` / `SpotLight` を動的に確保して使う。

```
kind                Point / Spot
color
startIntensity → endIntensity    強度の補間。5.0 → 0.0 で「光ってすぐ消える」
range               届く距離
lifetime
direction           Spot のみ。照射方向
spotCosAngle / spotCosFalloffStart   Spot の広がりと減衰開始
```

ライトのスロットは有限なので、大量に同時再生すると足りなくなる。

---

## Sound コンポーネント

```
soundName        SoundManager::LoadFile で登録した名前（[08_Audio.md](08_Audio.md)）
startTime        エフェクト開始からの遅延
distanceScale    3D 減衰スケール。大きいほど遠くまで聴こえる
volume
```

エフェクトの位置で 3D 再生される。**あらかじめ `SoundManager::LoadFile` で読み込んでおくこと。**

---

## EffectCurve（イージング）

`scaleCurve` / `posCurve` / `convergeCurve` で使う共通のカーブ。

- 横軸 `x` = 正規化時間（0〜1）、縦軸 `y` = 出力（0〜1）
- 制御点は `x` 昇順、`x`/`y` とも 0〜1
- 既定は `(0,0)-(1,1)` の直線＝そのまま線形
- `enabled = false` なら線形

Effect Editor 上でカーブエディタとして編集できる。

---

## 落とし穴

- **Effect Editor のプレビューは毎フレーム `EffectEditorWindow::Render()` を呼ばないと GPU 検証エラーで落ちる。** プレビュー RT がレンダーターゲット状態のまま `ImGui::Image` に渡されるため。`Sample` の `Draw()` を参考にすること
- **`EffectManager::Initialize()` を呼ばずに Effect Editor を開くと落ちる。** `GPUParticleManager` も必要
- `SetCamera()` を呼ばないとビルボードが正しく向かない
- `loop = true` のエフェクトは `Stop(handle)` を呼ばないと消えない。ハンドルを保持しておく
- Particle の `timeGroup` を `World` にしたまま時間を止めると、粒子も止まる。演出上止めたくないなら `Effect` にする

---

## 関連

- サウンドの登録 → [08_Audio.md](08_Audio.md)
- 時間グループ → [11_Utilities.md](11_Utilities.md)
- エディタ拡張 → [10_Editor.md](10_Editor.md)
