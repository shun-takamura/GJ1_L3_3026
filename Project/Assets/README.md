# Assets

元データの置き場。ここに置いたファイルはビルド時に自動変換され、
同じ相対パスで `Resources/` へ出力される。ゲーム側が読むのは `Resources/` のほう。

| 置くもの | 変換後 | 出力先 |
|---|---|---|
| `Textures/**.png` | BC7 圧縮 `.dds` | `Resources/Textures/**.dds` |
| `Models/**.obj` (+ `.mtl`) | `.mesh` / `.mat` | `Resources/Models/**` |
| `Models/**.gltf` (+ `.bin`) | `.mesh` / `.skel` / `.anim` / `.mat` | `Resources/Models/**` |
| `Sounds/**.wav` | そのままコピー | `Resources/Sounds/**` |
| `Fonts/**.ttf` | そのままコピー | `Resources/Fonts/**` |
| `*.hdr`（直下） | キューブマップ `.dds` | `Resources/Cubemaps/*.dds` |

`.fbx` は無視される（DCC でのオーサリング用に残しておいてよい）。
`.mtl` / `.bin` は `.obj` / `.gltf` の変換に吸収されるので単体では処理されない。

## 変換のタイミング

ビルドすると `CookAssets` ターゲットが走り、差分のあるファイルだけ変換される
（`tools/Python/sync_cache.json` が更新時刻を覚えている）。
手動で回したい場合は `Project/` をカレントにして次を実行する。

```
py tools\Python\cook_assets.py
```

## 変換後の使い方

- **SceneEditor** ウィンドウの Models / Textures / Materials 一覧に自動で現れる。
  そこからビューポートへドラッグ&ドロップするとシーンに配置される。
- **Effect Editor** のテクスチャ欄へドラッグ&ドロップすると、
  エフェクトのコンポーネントに割り当てられる。
- コードからは `Resources/...` のパスで直接読む
  （`TextureManager::LoadTexture("Resources/Textures/foo.dds")` など）。

一覧はディレクトリの更新を検知して自動リフレッシュされるが、
同名ファイルを上書きした場合は拾えないので SceneEditor の `Rescan Models` を押す。
