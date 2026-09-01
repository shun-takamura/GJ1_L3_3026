# ArcanaEngine

DirectX 12 の自作ゲームエンジン。この配布物はビルド済みの静的ライブラリと、
すぐ書き始められるテンプレートが入っている。

## 動かすまで

### 必要なもの

| | |
|---|---|
| Visual Studio | **v145 ツールセット**（同梱の `.lib` と同じ版が必要） |
| Windows SDK | 10 以降（`dxcompiler.dll` / `dxil.dll` に使う） |
| Python | 3.9 以降（アセット変換ツール。`py` コマンドが通ること） |

> **VS のバージョンが違うとリンクできない。** 静的ライブラリは STL / CRT の ABI に依存する。
> `C1047` や `LNK2038` が出たら、まずツールセットの版を疑う。

### 手順

1. `Project/MyGame.sln` を開く
2. `x64` / `Debug` でビルド
3. 実行すると、青い背景のタイトル画面が出る。**Space** で Game 画面へ、**WASD** で箱が動く

これが動けば環境は正しい。あとは `Template/` の中身を書き換えていく。

## 構成

```
Project/
  MyGame.sln            これを開く
  Common.props          エンジンのコンパイル設定（触らない）
  Template/             ★ここを書き換えてゲームを作る
  GameApp.{h,cpp}       アプリ本体
  main.cpp
  Scene/
    SceneManager        シーンのステート管理
    SceneFactory        シーン名 → 実体（シーン追加はここに1行）
    TitleScene / GameScene
    Transition/         フェード
  lib/{Debug,Development,Release}/   エンジンの静的ライブラリ
  DirectXGame/          エンジンのヘッダ
  Assets/               ★元データ（png / obj / gltf / wav）を置く
  Resources/            Assets から変換された実行時データ
  externals/            外部ライブラリ
  tools/                アセット変換・パッケージングツール
  bin/                  アセット圧縮ツール
  Documents/            ★リファレンス
Generated/              ビルド生成物（自動で作られる）
```

**`Project/` の階層は変えないこと。** エンジンは `Resources/` を `Project/` からの相対パスで読み、
ビルド生成物は `Generated/` へ出る。フォルダを移動するとパスが合わなくなる。

## まず読むもの

**[Project/Documents/README.md](Project/Documents/README.md)** が目次。使い方はここから辿る。

| | |
|---|---|
| 最初に | [01_GettingStarted.md](Project/Documents/01_GettingStarted.md) |
| モデルやスプライトを出す | [02_Rendering.md](Project/Documents/02_Rendering.md) |
| 自分の素材を追加する | [03_Assets.md](Project/Documents/03_Assets.md) |
| **うまくいかない** | [12_Pitfalls.md](Project/Documents/12_Pitfalls.md) |

## よくやること

### シーンを増やす

1. `Scene` を継承したクラスを作る（`TitleScene` が最小の見本）
2. `Project/Template/Scene/SceneFactory.cpp` に1行足す

```cpp
if (sceneName == "Result") { return std::make_unique<ResultScene>(); }
```

3. 遷移する

```cpp
SceneManager::GetInstance()->ChangeScene("Result", TransitionType::Fade);
```

### 素材を追加する

`Project/Assets/Textures/` に `.png` を置いてビルドすると、`Project/Resources/Textures/` に `.dds` として変換される。
SceneEditor の一覧に自動で出るので、ビューポートへドラッグ&ドロップで配置できる。

**`Resources/` に直接置いても読めない。** 必ず `Assets/` 側に置く。

### ウィンドウ名やアイコンを変える

[Documents/13_AppIcon.md](Project/Documents/13_AppIcon.md) を参照。

## 構成の使い分け

| 構成 | 用途 |
|---|---|
| `Debug` | ImGui エディタ有効。開発中は基本これ |
| `Development` | 最適化あり＋プロファイラ（P.E.P.P.E.R.）有効。動作を計測したいとき |
| `Release` | 配布用。ビルド後に `Distribution/` へ zip が作られる |

エディタ一式（Hierarchy / Inspector / SceneEditor / Effect Editor / Pepper）は
**`Debug` でのみコンパイルされる**。`Release` では丸ごと除外される。

## 注意

- **実行時のカレントディレクトリは `Project/`。** VS から実行する分には設定済み。
  ビルド出力の exe を直接ダブルクリックすると `Resources/` を見つけられず落ちる
- シェーダのソース（`.hlsl`）はこの配布物に含まれない。事前コンパイル済みの `.cso` を使う
- 不具合が出たら `Logs/<日時>/` をフォルダごと共有する。`crash_stack.txt` に関数名と行番号が出る

## 取り扱い

社外秘。再配布しないこと。
