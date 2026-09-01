# 08. サウンド

XAudio2 + X3DAudio。`SoundManager` はシングルトンで、`Initialize` は `Framework` が済ませてある。

対応形式は **`.wav`**。`Assets/Sounds/` に置くとビルド時に `Resources/Sounds/` へコピーされる（[03_Assets.md](03_Assets.md)）。

---

## 基本の流れ

**名前を付けて読み込み、名前で再生する。**

```cpp
#include "SoundManager.h"

auto* sm = SoundManager::GetInstance();

// --- 読み込み（シーンの Initialize などで一度だけ） ---
sm->LoadFile("bgm_stage1", "Resources/Sounds/stage1.wav");
sm->LoadFile("se_shot",    "Resources/Sounds/shot.wav");

// --- 再生 ---
sm->Play2DSound("bgm_stage1");
sm->Play2DSound("se_shot");

// --- 停止 ---
sm->Stop2DSound("bgm_stage1");

// --- 解放 ---
sm->Unload("se_shot");
```

### 毎フレーム更新が必要

```cpp
void MyScene::Update() {
    SoundManager::GetInstance()->Update();   // 終了検知と 3D DSP の更新
}
```

これを呼ばないと再生終了の検知が走らず、3D 音の定位も更新されない。

---

## 3D サウンド

位置に応じて音量とパンが変わる。ハンドルを返すので、動くものは毎フレーム位置を更新する。

```cpp
// リスナーはカメラ。毎フレーム更新する
sm->UpdateListener(GetCamera());

// 再生（ハンドルが返る）
uint32_t handle = sm->Play3DSound(
    "se_engine",
    enemyPos,                       // 位置
    enemyVelocity,                  // 速度（ドップラー用。省略可）
    20.0f);                         // distanceScale。大きいほど遠くまで聴こえる

// 動くものは毎フレーム更新
sm->UpdateEmitter(handle, enemyPos, enemyVelocity);

// 停止
sm->Stop3DSound(handle);
```

`distanceScale` が減衰のかかり方を決める。小さくすると近くでしか聴こえなくなる。

---

## 2D と 3D の使い分け

| | 用途 |
|---|---|
| `Play2DSound` | BGM、UI 音、プレイヤー自身の音など、定位が不要なもの |
| `Play3DSound` | 敵の足音、遠くの爆発、環境音など、位置が意味を持つもの |

---

## エフェクトから鳴らす

エフェクト定義に Sound コンポーネントを入れると、エフェクトの位置で 3D 再生される（[07_Effects.md](07_Effects.md)）。

```
soundName        SoundManager::LoadFile で登録した名前
startTime        エフェクト開始からの遅延（秒）
distanceScale
volume
```

**あらかじめ `LoadFile` で読み込んでおくこと。** 未登録の名前を指定しても鳴らない。

---

## 注意

- **`LoadFile` は毎フレーム呼ばない。** ファイル全体をメモリに展開するので、初期化時に一度だけ。効果音は使う分を先に全部読んでおく
- 同じ名前で `Play2DSound` を連打すると、実装によっては前の再生が止まる。同時多重再生が必要な効果音は 3D 側（ハンドル管理）を使うか、名前を分けて複数登録する
- `Update()` を呼び忘れると 3D の定位が固まる。`Scene::Update()` の定位置に入れておく
- `Stop3DSound` を呼ばずにハンドルを捨てると、鳴り終わるまで解放されない
- 配布 zip には `Resources/Sounds/` が含まれる（`.wav` は pack ではなくファイルシステムから直読みするため）

---

## 関連

- アセットの追加 → [03_Assets.md](03_Assets.md)
- エフェクトへの組み込み → [07_Effects.md](07_Effects.md)
