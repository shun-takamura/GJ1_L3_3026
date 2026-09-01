# 11. ユーティリティ

時間 / テキスト / デバッグ描画 / JSON / 数学 / 乱数 / ログ / プロファイラ。

---

## 時間管理と TimeGroup

**`dxCore_->GetDeltaTime()` を直接使わない。** それだとポーズやスローモーションが効かない。

時間は4つのグループに分かれていて、それぞれ独立した倍率を持つ。

```cpp
enum class TimeGroup { World, Player, UI, Effect, Count };
```

```cpp
#include "TimeGroup.h"

// Scene のメンバ関数として使う
float dtWorld  = GetScaledDeltaTime(TimeGroup::World);
float dtPlayer = GetScaledDeltaTime(TimeGroup::Player);
float dtUI     = GetScaledDeltaTime(TimeGroup::UI);
```

### 倍率を操作する

```cpp
SetTimeScale(TimeGroup::World, 0.05f);   // ワールドをほぼ停止
SetTimeScale(TimeGroup::Player, 1.0f);   // プレイヤーだけ通常速度
float s = GetTimeScale(TimeGroup::World);

// World だけを触る短縮版
SetSceneTimeScale(0.0f);                 // ポーズ
float ss = GetSceneTimeScale();
```

**演出例**

| 表現 | World | Player | UI |
|---|---|---|---|
| ヒットストップ | 0.05 | 0.05 | 1.0 |
| ジャスト回避 | 0.3 | 1.0 | 1.0 |
| ポーズ | 0.0 | 0.0 | 1.0 |

グローバル倍率は `dxCore_->SetTimeScale(0.5f)` で、全グループに掛かる。

### シーンの経過時間

```cpp
float t = GetElapsedSeconds();
TickElapsedSeconds(dt);        // 通常は自動
Seek(12.5f);                   // 指定秒へ飛ばす（override して自前の巻き戻しを実装できる）
```

`GetSeekMaxSeconds()` を override して総尺を返すと、ImGui の TimeControler で**シークバーが有効になる**。

### EngineTime

エンジン内部（パーティクル等）が現在のシーンのタイムスケールを参照するための窓口。アプリ側で意識する必要はないが、`Scene` 以外の場所で `TimeGroup` 付き delta が欲しい場合に使う。

---

## テキスト描画

```cpp
#include "TextRenderer.h"

// 初期化（Framework が済ませているが、フォントを変えたい場合）
TextRenderer::GetInstance()->Initialize(dxCore_, srvManager_,
    "Resources/Fonts/MPLUS1p-Regular.ttf", 32, 1024);

// 描画（毎フレーム。UTF-8 で渡す）
auto* tr = TextRenderer::GetInstance();
tr->DrawText(u8"スコア: 12345", { 40.0f, 30.0f }, 1.0f);

// 幅を測ってセンタリング
float w = tr->MeasureWidth(u8"GAME OVER", 2.0f);
tr->DrawText(u8"GAME OVER", { (1280.0f - w) * 0.5f, 300.0f }, 2.0f);

// スプライトと同じタイミング（描画順の最後）で確定させる
tr->Flush();
```

アウトライン色も指定できる（既定は黒）。日本語は同梱の M PLUS 1p で表示できる。

---

## デバッグ描画

線でワイヤーを描く。Debug ビルド用。

```cpp
#include "Primitive/DebugDraw.h"

DebugDraw::Sphere({ 0, 1, 0 }, 2.0f, { 0, 1, 0, 1 }, 16);
DebugDraw::AABB(minPos, maxPos, { 1, 1, 0, 1 });
DebugDraw::OBB(center, axes, halfSize, { 1, 0, 0, 1 });
DebugDraw::Capsule(center, axes, height, radius, { 0, 1, 1, 1 }, 16);
DebugDraw::Line(a, b, { 1, 1, 1, 1 });
DebugDraw::Ray(origin, dir, 10.0f, { 1, 0, 1, 1 });
DebugDraw::Cross(pos, 0.5f, { 1, 1, 1, 1 });
DebugDraw::Grid({ 0, 0, 0 }, 100.0f, 1.0f, { 0.3f, 0.3f, 0.3f, 1 });
DebugDraw::CatmullRomSpline(controlPoints, { 1, 0.5f, 0, 1 });
```

積んだ線は `LineRenderer` がまとめて描く。コスト目安は Sphere(seg=16) が 48 本、AABB / OBB が 12 本。

---

## JSON

**外部ライブラリは使わず自作**。設定ファイルやデータ駆動に使う。

```cpp
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include "Json/JsonValue.h"

// --- 読む ---
auto result = JsonParser::ParseFile("Resources/Json/Setting/config.json");
if (!result.errorMessage.empty()) {
    Log("JSON parse error: " + result.errorMessage);
    return;
}
const JsonValue& root = result.value;
float speed = root["player"]["speed"].GetFloat();

// 文字列から
auto r2 = JsonParser::Parse(R"({"hp": 100})");

// --- 書く ---
JsonValue out;
out["name"] = "Player";
out["hp"] = 100;
JsonWriter::WriteFile("Resources/Json/Setting/save.json", out);

// 文字列として得る
std::string text = JsonWriter::Write(out);
```

エフェクト定義、シーン JSON、プレハブ、キーコンフィグはすべてこの仕組みで動いている。

---

## 数学

`GameEngine/Math/` にある。`DirectXMath` の型を外へ漏らさない方針。

| 型 | 用途 |
|---|---|
| `Vector2` / `Vector3` / `Vector4` / `Vector2Int` | ベクトル |
| `Matrix3x3` / `Matrix4x4` | 行列 |
| `Quaternion` / `QuaternionTransform` | 回転（ジンバルロック回避） |
| `Transform` | scale / rotate / translate のセット |
| `Frustum` | 視錐台。カリング判定 |

```cpp
#include "MathUtility.h"

Matrix4x4 world = MakeAffineMatrix(transform);
Matrix4x4 rot   = MakeRotateMatrix(eulerRadians);
Matrix4x4 inv   = Inverse(matrix);
Vector3 p       = TransformCoordinate(localPos, matrix);
Vector3 n       = Normalize(v);
float rad       = DegToRad(90.0f);
float deg       = RadToDeg(rad);
```

### イージングと補間

```cpp
#include "Easing.h"
#include "Interpolator.h"

float t = Easing::Apply(Easing::Type::EaseOutCubic, ratio);
float v = Easing::EaseOutCubic(ratio);
float l = Easing::Linear(ratio);
```

エフェクトのカーブ（`EffectCurve`）も同じ考え方で、時間 `t` を再マップしてから補間する。

---

## 乱数

**`rand()` を直接使わない。** リプレイ機能がシードから再現するため、中央管理の乱数を通す。

```cpp
#include "RandomGenerator.h"

auto& rng = RandomGenerator::Instance();
float f01 = rng.NextFloat01();            // 0.0 ~ 1.0
float f   = rng.NextFloat(-1.0f, 1.0f);
int   i   = rng.NextInt(0, 9);            // 両端含む
```

シードは `Framework::Initialize` が設定する。起動引数 `--seed 12345` で固定でき、`--replay <dir>` では `session.log` から復元される。

---

## ログ

```cpp
#include "Log.h"

Log("プレイヤーがスポーンしました\n");     // Session カテゴリ / Info レベル
```

より細かく制御したい場合は `SessionLogger` を直接使う。

```cpp
#include "SessionLogger.h"

SessionLogger::Instance().Write(
    SessionLogger::Category::Error,
    SessionLogger::Level::Error,
    "モデルの読み込みに失敗: " + path);
```

### カテゴリとファイル

出力先は `Logs/<日時>/`。

| カテゴリ | ファイル | 内容 |
|---|---|---|
| `Input` | `input.log` | 入力アクションの押下/解放（リプレイに使う） |
| `State` | `state.log` | フレーム / 座標 / HP / シーン |
| `Event` | `event.log` | death / gameover / シーン遷移など |
| `Gfx` | `gfx.log` | DirectX 警告・デバイスロスト |
| `Error` | `error.log` | 一般エラー |
| `Session` | `session.log` | KPI・診断・一般 INFO |
| `Profile` | `profile.log` | P.E.P.P.E.R. の区間計測 |

### レベル

`Critical / Error / Warn / Info / Debug / Trace`（値が小さいほど深刻）。

- **Debug ビルド** — `Trace` まで全部出る
- **Release / Development** — `Error` 以上だけ出る

つまり **`Log()`（Info）は配布ビルドでは出ない**。配布ビルドでも残したい情報は `Level::Error` で書く。

カテゴリ単位で閾値を変えられる。

```cpp
SessionLogger::Instance().SetCategoryLevel(
    SessionLogger::Category::State, SessionLogger::Level::Info);
```

### クラッシュ

`CrashHandler::Install()` を `WinMain` の先頭で呼んでおくと、未捕捉例外時に `Logs/<日時>/` へ `crash.dmp` と `crash_stack.txt` が残る。`crash_stack.txt` は関数名とソース行まで出るので、まずこれを見る。

**不具合を報告するときは `Logs/<日時>/` のフォルダごと添付する。**

---

## プロファイラ（P.E.P.P.E.R.）

`Development` 構成（`USE_PEPPER` 定義時）で有効。未定義時はマクロが空に展開されるので、コードに残したままでよい。

```cpp
#include "PepperMacros.h"

void MyScene::Update() {
    PEPPER_SCOPE("MyScene::Update");          // CPU 区間

    {
        PEPPER_SCOPE("MyScene::UpdateEnemies");
        for (auto& e : enemies_) e->Update(dt);
    }
}

void MyScene::Draw() {
    PEPPER_GPU_SCOPE(dxCore_->GetCommandList(), "MyScene::Draw");   // GPU 区間
    // ...
}
```

結果は ImGui の **Pepper** パネルで見る（毎秒更新・60fps 基準の予算表示）。`Logs/<日時>/profile.log` にも残る。

---

## 関連

- ログの見方と不具合報告 → [12_Pitfalls.md](12_Pitfalls.md)
- 入力とリプレイ → [05_Input.md](05_Input.md)
