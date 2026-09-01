# 03. アセット — Assets から Resources へ

## 大原則

**元データは `Assets/` に置く。コードから読むのは `Resources/`。**

`Resources/` の中身はビルド時に自動生成される。`.png` を直接 `Resources/Textures/` に置いてもエンジンは読めない（読むのは `.dds`）。ここを間違えるのが最初の関門。

```
Assets/Textures/enemy.png
        │  ビルド時に cook_assets.py が変換
        ▼
Resources/Textures/enemy.dds        ← コードやエディタが使うのはこちら
```

---

## 変換ルール

| `Assets/` に置くもの | 変換後 | 出力先 |
|---|---|---|
| `Textures/**.png` | BC7 圧縮 `.dds` | `Resources/Textures/**.dds` |
| `Models/**.obj`（+ `.mtl`） | `.mesh` / `.mat` | `Resources/Models/**` |
| `Models/**.gltf`（+ `.bin`） | `.mesh` / `.skel` / `.anim` / `.mat` | `Resources/Models/**` |
| `Sounds/**.wav` | そのままコピー | `Resources/Sounds/**` |
| `Fonts/**.ttf` | そのままコピー | `Resources/Fonts/**` |
| `*.hdr`（`Assets/` 直下） | キューブマップ `.dds` | `Resources/Cubemaps/*.dds` |

- `.fbx` は**無視される**。DCC でのオーサリング用に置いておいてよい
- `.mtl` / `.bin` は `.obj` / `.gltf` の変換に吸収されるので単体では処理されない
- サブフォルダ構成はそのまま維持される

### アセット形式について

`.mesh` は submesh 対応のバイナリ形式で、部位ごとに別マテリアル・別テクスチャを持てる。テクスチャが指定されていない submesh は白でフォールバックする。

---

## 変換のタイミング

ビルドすると `CookAssets` ターゲットが走り、**差分のあるファイルだけ**変換される（`tools/Python/sync_cache.json` が更新時刻を覚えている）。

手動で回す場合はカレントディレクトリを `Project/` にして実行する。

```
py tools\Python\cook_assets.py
```

ビルド時に走るのは次の3段階。

```
convert_hdr_to_dds.py   Assets/*.hdr → Resources/Cubemaps/*.dds
cook_assets.py          Assets/**    → Resources/**
pack_assets.py          Resources/** → Generated/Assets.pack
```

---

## 変換後の使い方

### SceneEditor から

変換されたアセットは **SceneEditor ウィンドウの一覧に自動で現れる**。

| セクション | 中身 | ドロップ先 |
|---|---|---|
| Models (.mesh) | 静的モデル | ビューポート → シーンに配置 |
| Sprites (.dds) | テクスチャ | ビューポート → 2D 配置 / Inspector のテクスチャ欄 / Effect Editor |
| Materials (.mat) | マテリアル | Inspector のマテリアル欄 |
| Animations (.anim) | アニメーション | Inspector のアニメ欄 |
| Animated | スキン付きモデル | ビューポート |
| Primitives | Box/Sphere など | ビューポート |
| Effects | エフェクト定義 | Inspector の Effects 欄 |

一覧はディレクトリの更新を検知して自動リフレッシュされる。ただし**同名ファイルを上書きした場合は拾えない**（ディレクトリの更新時刻が動かないため）。その場合は `Rescan Models` ボタンを押す。

### コードから

`Resources/` からの相対パスをそのまま渡す。

```cpp
obj_->Initialize(object3DManager_, dxCore_, "Resources/Models/Enemy", "enemy.mesh");
sprite_->Initialize(spriteManager_, "Resources/Textures/hp_gauge_fill.dds");
TextureManager::GetInstance()->LoadTexture("Resources/Textures/white1x1.dds");
SoundManager::GetInstance()->LoadFile("bgm", "Resources/Sounds/stage1.wav");
```

---

## パックと DirectStorage

`pack_assets.py` が `Resources/` を 1 個の `Generated/Assets.pack` に固める。実行時は `AssetLocator` がパスを解決し、DirectStorage + GDeflate で読み込む。

- **Pack があればそちらが使われる**（`[KPI] mode=Pack+DStorage`）
- 無ければファイルシステムから直読み（`mode=FS`）
- コードから見たパスは**どちらでも同じ**。`Resources/...` と書けばよい

ImGui の `Assets` メニューから経路を切り替えて確認できる。起動時引数 `--use-pack` / `--use-fs` でも指定可能。

> `Resources/Shaders/` と `Resources/CompiledShaders/` は pack に含まれない。シェーダは DirectXCore が個別にファイルから読むため（[04_Shaders.md](04_Shaders.md)）。

---

## 新しい素材を追加する手順

1. `Assets/Textures/` に `.png` を置く（サブフォルダを切ってよい）
2. ビルドする
3. SceneEditor の Sprites 一覧に現れる
4. ビューポートや Inspector のテクスチャ欄へドラッグ&ドロップ

モデルなら `Assets/Models/<名前>/` にフォルダを作り、`.obj` + `.mtl` + テクスチャ、または `.gltf` + `.bin` + テクスチャを一式で置く。

---

## 注意

- **`Resources/` を直接編集しない。** 次のビルドで上書きされる。必ず `Assets/` 側を直す
- **`Resources/` はコミットする。** 変換結果もリポジトリに入れておくことで、Python やツールが無い環境でもビルド・実行できる
- **非 ASCII のパスは避ける。** ファイル名・フォルダ名に日本語が入ると変換ツールが落ちることがある
- キューブマップ用の `.hdr` は `Assets/` 直下に置く（サブフォルダは見ない）

---

## 関連

- 配布物に何が入るか → [12_Pitfalls.md](12_Pitfalls.md)
- SceneEditor の拡張 → [10_Editor.md](10_Editor.md)
