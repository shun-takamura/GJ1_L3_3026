# 04. シェーダ

## 置き場所と命名

`Resources/Shaders/**` に置く。**ファイル名でシェーダの種類が決まる**ので、命名規則は必ず守る。

```
Resources/Shaders/
  Object3D/Object3d.VS.hlsl      ← 頂点シェーダ  → vs_6_0
  Object3D/Object3d.PS.hlsl      ← ピクセルシェーダ → ps_6_0
  Skinning/Skinning.CS.hlsl      ← コンピュートシェーダ → cs_6_0
  hlsli/Common.hlsli             ← インクルード専用（単体ではコンパイルされない）
```

| 2段目の拡張子 | プロファイル |
|---|---|
| `.VS.hlsl` | `vs_6_0` |
| `.PS.hlsl` | `ps_6_0` |
| `.CS.hlsl` | `cs_6_0` |
| `.GS` / `.HS` / `.DS` | 対応するプロファイル |

エントリポイントは **必ず `main`**。

---

## コードから読む

**取得は必ず `DirectXCore::LoadShaderBlob()` を通す。** 引数には常に `.hlsl` のパスを渡す。`.cso` のパスは内部で導出される。

```cpp
IDxcBlob* vs = dxCore_->LoadShaderBlob(L"Resources/Shaders/MyEffect/MyEffect.VS.hlsl", L"vs_6_0");
IDxcBlob* ps = dxCore_->LoadShaderBlob(L"Resources/Shaders/MyEffect/MyEffect.PS.hlsl", L"ps_6_0");

D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
// ... 以下 PSO 生成
```

### Debug と Release で挙動が変わる

| 構成 | 動作 |
|---|---|
| `Debug` | `.hlsl` を実行時に DXC でコンパイル。**書き換えて再実行するだけで反映される** |
| `Release` / `Development` | ビルド時に生成された `Resources/CompiledShaders/**.cso` を読む。起動が速い |

`.cso` が見つからない場合は実行時コンパイルにフォールバックし、`error.log` に記録される。配布ビルドでこのログが出ていたらクック漏れか作業ディレクトリ違い。

---

## 新しいシェーダを追加する手順

1. `Resources/Shaders/<カテゴリ>/` に `Foo.VS.hlsl` / `Foo.PS.hlsl` を作る
2. エントリポイントを `main` にする
3. `ArcanaEngine.vcxproj` に `<None Include="..\Resources\Shaders\<カテゴリ>\Foo.VS.hlsl" />` を追加する（VS のツリーに出すため。ビルドには不要）
4. コードから `LoadShaderBlob` で読む
5. `Debug` で動作確認 → `Release` でビルドすると `.cso` が自動生成される

`.cso` の生成は `Debug` ではスキップされる（実行時コンパイルするため不要）。

---

## 事前コンパイル

```
py tools\Python\compile_shaders.py            # 差分のみ
py tools\Python\compile_shaders.py --force    # 全部やり直す
py tools\Python\compile_shaders.py --clean    # 出力を消す
```

`Release` / `Development` のビルド時に自動実行される（`CompileShaders` ターゲット）。

- `dxc.exe` は Windows SDK から自動検出される
- `.hlsli` が1つでも更新されたら**全シェーダをコンパイルし直す**（インクルード依存を追わない安全側の判断）
- 出力は `Resources/CompiledShaders/` に元のフォルダ構成を保って置かれる。ここは生成物なので `.gitignore` 対象

### コンパイルオプション

```
-E main -Zpr -O3
```

> **`-Zpr`（行優先メモリレイアウト）は絶対に変えない。**
> 実行時コンパイル側も `-Zpr` を使っている。ここが食い違うと**行列が転置されて描画が壊れる**。「Release だけ描画がおかしい」場合はまずこれを疑う。

実行時コンパイルは `-Od`（最適化なし）＋デバッグ情報付き、事前コンパイルは `-O3`。挙動が変わることはまずないが、Release でだけ見た目が違う場合はここも候補。

---

## 配布物

`.hlsl` は配布 zip に**含まれない**。入るのは `Resources/CompiledShaders/**.cso` だけ。シェーダのソースを渡さずに済む。

---

## インクルード

共通処理は `Resources/Shaders/hlsli/` に `.hlsli` として置く。`compile_shaders.py` はこのフォルダを `-I` で渡すので、単純な名前で include できる。

```hlsl
#include "Common.hlsli"
```

`.hlsli` 自体は単体でコンパイルされない（`.VS` `.PS` などが付いていないため自動的にスキップされる）。

---

## 関連

- 描画パイプラインの組み立て → [02_Rendering.md](02_Rendering.md)
- ポストエフェクトのフィルタ追加 → [09_PostEffect.md](09_PostEffect.md)
