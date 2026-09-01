# GJ1_L3_3026

ArcanaEngine（自作 DirectX 12 エンジン）を使ったチーム制作。

エンジンはビルド済みの静的ライブラリとして同梱してある。**クローンすればすぐビルドできる。**

---

## セットアップ

### 必要なもの

| | |
|---|---|
| Visual Studio | **v145 ツールセット**（同梱の `.lib` と同じ版が必要） |
| Windows SDK | 10 以降 |
| Python | 3.9 以降（アセット変換に使う。`py` コマンドが通ること） |
| Git LFS | 任意だが推奨（`.vcxproj` の排他ロックに使う） |

> **VS のバージョンが違うとリンクできない。** 静的ライブラリは STL / CRT の ABI に依存する。
> `C1047` や `LNK2038` が出たら、まずツールセットの版を疑う。

### クローン後にやること

**1. mergiraf のセットアップ（1回だけ・全員）**

`Project/tools/mergiraf/setup_mergiraf.bat` をエクスプローラーでダブルクリック。

`git merge` は行単位でしか見ないので、**二人が同じクラスに別々の関数を足しただけ**でもコンフリクトになる。mergiraf は C++ を構文木として解析するのでこれを自動で解決する。

登録先の `.git/config` はバージョン管理されないため、**各自が1回ずつ実行する必要がある**。

**2. git-lfs（推奨）**

```
git lfs install
```

`.vcxproj` は全員が同じ場所を書き換えるので衝突しやすい。編集前にロックを取る。

```
git lfs lock   Project/Game/GJ1_L3_3026.vcxproj
git lfs unlock Project/Game/GJ1_L3_3026.vcxproj
```

**push したら必ず解除する。** 誰が何をロック中かは `git lfs locks` で分かる。

**3. ビルド**

`Project/GJ1_L3_3026.sln` を開いて `x64` / `Debug` でビルド・実行。
青い背景のタイトル画面が出て、**Space** でゲーム画面へ、**WASD** で箱が動けば成功。

---

## 開発の進め方

### ゲームコードを書く場所

**`Project/Game/`** の中。ここが自分たちのコード。

```
Project/Game/
  GameApp.{h,cpp}       アプリ本体（Draw の組み立て・各種フックの配線）
  main.cpp
  Scene/
    SceneManager        シーンのステート管理
    SceneFactory        シーン名 → 実体（★シーン追加はここに1行）
    TitleScene          タイトル画面
    GameScene           ゲーム本編（ここから作り始める）
    Transition/         フェード
```

エンジン側（`Project/DirectXGame/`、`Project/lib/`）は**触らない**。

### シーンを増やす

1. `Scene` を継承したクラスを作る（`TitleScene` が最小の見本）
2. `Project/Game/Scene/SceneFactory.cpp` に1行足す

```cpp
if (sceneName == "Result") { return std::make_unique<ResultScene>(); }
```

3. 遷移する

```cpp
SceneManager::GetInstance()->ChangeScene("Result", TransitionType::Fade);
```

**新しい `.cpp` / `.h` を作ったら `GJ1_L3_3026.vcxproj` と `.filters` の両方に追加する。**
このとき `git lfs lock` を取ってから編集すると衝突しない。

### 素材を追加する

`Project/Assets/Textures/` に `.png` を置いてビルドすると、`Project/Resources/Textures/` に `.dds` として変換される。SceneEditor（Debug ビルドで起動する ImGui のウィンドウ）の一覧に自動で出るので、ビューポートへドラッグ&ドロップで配置できる。

**`Resources/` に直接置いても読めない。** 必ず `Assets/` 側に置く。

モデルは `Project/Assets/Models/<名前>/` に `.obj`+`.mtl` か `.gltf`+`.bin` を一式で置く。

### 構成の使い分け

| 構成 | 用途 |
|---|---|
| `Debug` | ImGui エディタ有効。**開発中は基本これ** |
| `Development` | 最適化あり＋プロファイラ有効。重い場所を探すとき |
| `Release` | 提出用。ビルド後に `Generated/Output/Release/Distribution/` へ zip ができる |

エディタ一式（Hierarchy / Inspector / SceneEditor / Effect Editor）は **`Debug` でのみ**動く。

---

## ドキュメント

**[Project/Documents/README.md](Project/Documents/README.md)** が目次。

| | |
|---|---|
| 最初に読む | [01_GettingStarted.md](Project/Documents/01_GettingStarted.md) |
| モデル・スプライトを出す | [02_Rendering.md](Project/Documents/02_Rendering.md) |
| 素材を追加する | [03_Assets.md](Project/Documents/03_Assets.md) |
| 入力 | [05_Input.md](Project/Documents/05_Input.md) |
| 当たり判定 | [06_Collision.md](Project/Documents/06_Collision.md) |
| エフェクト | [07_Effects.md](Project/Documents/07_Effects.md) |
| サウンド | [08_Audio.md](Project/Documents/08_Audio.md) |
| **うまくいかない** | [12_Pitfalls.md](Project/Documents/12_Pitfalls.md) |

---

## 困ったら

まず [12_Pitfalls.md](Project/Documents/12_Pitfalls.md) を見る。よくあるものを挙げておく。

| 症状 | 原因 |
|---|---|
| Hierarchy に何も出ない | `IImGuiEditable::SetHooks()` の配線 |
| 画面が真っ暗 | 平行光源の向きを設定していない |
| コライダーが当たらない | `collider.enabled = true` を忘れている |
| `.png` が読めない | `Resources/` に直接置いている（`Assets/` に置く） |
| exe を直接起動すると落ちる | 作業ディレクトリが `Project/` でない。VS から実行する |
| `.vcxproj` が編集できない | git-lfs のロック。`git lfs lock` を取る |

不具合を報告するときは **`Project/Logs/<日時>/` をフォルダごと**共有する。`crash_stack.txt` に関数名と行番号が出る。

---

## 注意

- **エンジンのソースは含まれていない。** ヘッダと `.lib` のみ
- **`Project/` の階層は変えない。** エンジンが `Resources/` を `Project/` 相対で読む
- エンジンに不具合を見つけたら直接直せないので、エンジン担当に伝える
