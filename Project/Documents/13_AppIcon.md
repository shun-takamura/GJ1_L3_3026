# 13. アプリのアイコンとタイトル

タイトルバー・タスクバー・エクスプローラーに出る見た目を変える。

**アイコンは2種類の仕組みで別々に決まる。** 片方だけ設定すると「エクスプローラーでは変わったのにタイトルバーは既定のまま」といった状態になるので、両方やる。

| 場所 | 決まり方 |
|---|---|
| エクスプローラー / タスクバー / ショートカット | exe に埋め込んだ **ICON リソース**を Windows が自動で拾う |
| タイトルバー / Alt+Tab | コードで **`WM_SETICON` / ウィンドウクラス**に設定する |

同じ `.ico` を使い回せるので、**用意するファイルは1つ**でよい。

---

## タイトルだけ変える

コード1行で済む。`.ico` は不要。

```cpp
// MyApp.h
class MyApp : public Framework {
public:
    const wchar_t* GetWindowTitle() const override { return L"My Awesome Game"; }
    ...
};
```

既定は `L"ArcanaEngine"`。

---

## アイコンを変える

### 1. `.ico` を用意する

**複数サイズを1ファイルに内包**させる。Windows が場面ごとに使い分けるので、16px しか入っていないとエクスプローラーの大アイコン表示でぼやける。

推奨サイズ: **256 / 48 / 32 / 16 px**

PNG からの変換例。

```
magick convert icon.png -define icon:auto-resize=256,48,32,16 app.ico
```

置き場所は**プロジェクトのフォルダ直下**（例: `Sample/app.ico`）。`.rc` から相対参照するため、`Assets/` には置かない（アセットパイプラインの変換対象にしてしまうため）。

### 2. リソーススクリプトを書く

`Sample/app.rc` を作る。

```rc
#include "resource.h"

IDI_APPICON ICON "app.ico"
```

`Sample/resource.h` に ID を定義する。

```c
#pragma once

#define IDI_APPICON  101
```

> **ID の数値は小さいほどよい。** Windows は exe 内で**一番小さい番号の ICON リソース**をファイルアイコンとして使う。複数のアイコンを持たせる場合、アプリ本体のアイコンに最小の番号を割り当てる。

### 3. vcxproj に登録する

`.rc` は `ClCompile` ではなく **`ResourceCompile`** で登録する。

```xml
<ItemGroup>
  <ResourceCompile Include="app.rc" />
</ItemGroup>
```

`.ico` と `resource.h` は `<None>` / `<ClInclude>` で登録しておくと VS のツリーに出る（ビルドには不要）。

**この時点でエクスプローラーとタスクバーのアイコンは変わる。** リソースが埋め込まれれば Windows が勝手に拾うため、コードは不要。

### 4. タイトルバーにも反映する

`Framework::GetWindowIconResourceId()` を override する。

```cpp
// MyApp.h
#include "resource.h"

class MyApp : public Framework {
public:
    const wchar_t* GetWindowTitle() const override { return L"My Awesome Game"; }
    int GetWindowIconResourceId() const override { return IDI_APPICON; }
    ...
};
```

既定は `0` で、その場合は Windows の既定アイコンになる。**アイコンを用意しないなら何もしなくてよい。**

エンジン側では `WindowsApplication::Initialize` がこの ID を受け取り、

- Alt+Tab / タスクバー用の大きいアイコン → ウィンドウクラスの `hIcon`
- タイトルバー用の小さいアイコン → ウィンドウ生成後に `WM_SETICON`

をそれぞれ最適なサイズで読み込む。`WNDCLASS` には小さいアイコン用のフィールドが無いため、この2段構えになっている。

---

## 確認と落とし穴

### 変えたのに反映されない

- **エクスプローラーのアイコンキャッシュ**が残っていることがある。別のフォルダにコピーする、explorer を再起動する、あるいはファイル名を変えて確認する
- ビルド出力フォルダの古い exe を見ていないか
- `.rc` が `ResourceCompile` で登録されているか（`ClCompile` に入れると失敗する）

### タイトルバーだけ既定のまま

`GetWindowIconResourceId()` の override を忘れている。exe のアイコン（リソース）とタイトルバーのアイコン（コード）は独立している。

### アイコンがぼやける

`.ico` に大きいサイズが入っていない。256px を含めて作り直す。

### `.rc` を追加したらビルドが通らない

- `resource.h` のパスが合っているか
- `.rc` の文字コード。日本語を含めるなら UTF-16LE か、そもそも含めない
- `.ico` のパスが `.rc` からの相対で合っているか

---

## 関連

- ウィンドウサイズは `WindowsApplication::kClientWidth / kClientHeight` を参照 → [11_Utilities.md](11_Utilities.md)
- 配布物の作り方 → リポジトリルートの [README.md](../README.md)
