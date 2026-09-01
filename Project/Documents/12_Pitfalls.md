# 12. 落とし穴集

**うまくいかないときに最初に見るページ。** ここに挙げたものは実際に踏まれたもの。

---

## 起動・実行

### 起動直後にクラッシュする / 真っ黒

**カレントディレクトリが `Project/` になっていない。**

エンジンは `Resources/` を `Project/` からの相対パスで読む。

| 起動方法 | 動くか |
|---|---|
| VS から実行 | ✅ `.vcxproj` の `LocalDebuggerWorkingDirectory` が効く |
| ビルド出力の exe を直接ダブルクリック | ❌ `Generated/Output/<構成>/` に `Resources/` は無い |
| 配布 zip を展開して実行 | ✅ zip には `Resources/` が同梱される |

`Logs/<日時>/error.log` に `cwd` が出るので確認できる。

### ビルド後の DLL コピーで失敗する（`MSB3073` / `MSB3030`）

**パスに日本語が含まれていないか。** `PostBuildEvent` は MSBuild が一時バッチに書き出して
実行するため、コードページの都合で非 ASCII パスの `copy` が失敗する。
配布物のテンプレートは MSBuild の `Copy` タスクを使っているのでこの問題は起きないが、
自分で `PostBuildEvent` を足すときは注意する。

**`dxil.dll` が見つからない場合**、その Windows SDK バージョンに同梱されていない。
SDK は複数バージョンが共存でき、`dxcompiler.dll` はあるのに `dxil.dll` は無い、
ということが起きる（10.0.26100.0 が該当）。テンプレートはワイルドカードで
SDK の全バージョンの `bin/<version>/x64/` を探すので、どれか1つに入っていれば拾える。

### `Development` / `Release` の初回ビルドで `pack_assets` が失敗する

`gdeflate_compress.exe` が `dstorage.dll` を見つけられない（終了コード `3221225781` = `0xC0000135`）。DLL を配置する PostBuildEvent が cook の**後**に走るため。

`Sample.vcxproj` の `CookAssets` ターゲット冒頭で DLL をコピーするようにしてあるので、現在は解消済み。もし再発したら `Generated/Output/<構成>/` に `dstorage.dll` / `dstoragecore.dll` を手でコピーする。

### ウィンドウのタイトルを変えたい

`Framework::GetWindowTitle()` を override する。既定は `L"ArcanaEngine"`。

```cpp
// MyApp.h
class MyApp : public Framework {
public:
    const wchar_t* GetWindowTitle() const override { return L"My Awesome Game"; }
    ...
};
```

タイトルバーの表示だけで、ウィンドウクラス名や exe 名とは無関係。

### `DebugDraw` で描いた線が見えない

**`DebugDraw::*` は線をキューに積むだけ。** フレームの最後に `LineRenderer` を描かないと何も出ない。

```cpp
void MyScene::Draw() {
    // ... 通常の描画 ...

    DebugDraw::Grid({ 0, 0, 0 }, 20.0f, 1.0f, { 0.4f, 0.4f, 0.5f, 1.0f });
    DebugDraw::Sphere(pos, 1.0f, { 1, 0, 0, 1 });

    // ★これを忘れると線が出ない
    auto* lr = LineRenderer::GetInstance();
    lr->SetCamera(GetCamera());   // カメラ未設定だと描画位置が定まらない
    lr->Draw();
}
```

見えない場合の確認点。

- `LineRenderer::Draw()` を呼んでいるか
- `SetCamera()` を呼んでいるか
- 線が地面と同じ高さで Z ファイティングしていないか（グリッドは `y = 0.01f` など少し浮かせる）
- 色のアルファが 0 になっていないか
- `CollisionSystem` のデバッグ描画も同じ仕組みなので、そちらが見えていれば `LineRenderer` は動いている

### 画面に何も出ない

- **平行光源の向きを設定していない。** 既定が 0 ベクトルだと真っ暗になる。シーンの `Initialize` で `LightManager::GetInstance()->SetDirectionalLightDirection(...)` を呼ぶ
- `Scene::GetCamera()` を override していない / `Update()` を呼んでいない
- `object3DManager_->SetDefaultCamera()` を呼んでいない

---

## エディタ

### Hierarchy / Inspector に何も出ない

**`IImGuiEditable::SetHooks()` を配線していない。** エンティティの登録はこのフック経由なので、未配線だと何も載らない。`MyApp::Initialize()` の**先頭**（他の何よりも先）で呼ぶ。

### Scene ビューポートが `RenderTexture not set`

`ImGuiManager::SetViewportRenderTexture()` を呼んでいない。シーンを描く先の `RenderTexture` を渡す必要がある。

### Effect Editor を開いた瞬間に GPU 検証エラーで落ちる

```
GPU_BASED_VALIDATION_INCOMPATIBLE_TEXTURE_LAYOUT
```

**`EffectEditorWindow::Render()` を毎フレーム呼んでいない。** プレビュー RT がレンダーターゲット状態のまま `ImGui::Image` に渡されるのが原因。`Draw()` の中、ImGui の `EndFrame()` より前に呼ぶ。

あわせて `EffectManager::Initialize()` と `GPUParticleManager` の初期化も必要。

### PostEffect パネルが空

`getPostEffect` フックが未配線。`ImGuiManager::SetHostHooks()` で配線する。

### 追加したウィンドウが出てこない

`imgui.ini` に既存レイアウトが保存されているため、新規ウィンドウはフローティングで出る。`ImGuiMenu` から表示を確認して手でドッキングする。

---

## ビルド

### `C2338 can't delete an incomplete type` / `C2027 認識できない型 'Xxx'` / `C4150`

**このエンジンで最も繰り返し踏まれている問題。** 原因は毎回同じ。

> **前方宣言だけの型を `std::unique_ptr` で持つクラスの、コンストラクタ／デストラクタをヘッダに書いている。**

```cpp
// ❌ 悪い例
class SpriteInstance;                      // 前方宣言だけ

class FadeTransition {
public:
    FadeTransition() = default;            // ← ヘッダに実装がある
    ~FadeTransition() = default;           // ← これが原因
private:
    std::unique_ptr<SpriteInstance> sprite_;
};
```

ヘッダに実装があると、**このクラスを構築する／破棄する翻訳単位すべて**で `SpriteInstance` の完全型が要求される。`unique_ptr` の削除子が `static_assert(sizeof(T) > 0)` を持っているため。

```cpp
// ✅ 正しい例
// FadeTransition.h
FadeTransition();                          // 宣言だけ
~FadeTransition() override;

// FadeTransition.cpp（SpriteInstance.h を include している）
FadeTransition::FadeTransition()  = default;
FadeTransition::~FadeTransition() = default;
```

**コンストラクタ側も必ず `.cpp` へ出す。** メンバ構築が途中で例外を投げたときの巻き戻しコードが `unique_ptr` の破棄を要求するため、デストラクタだけ直しても解決しない。

**デストラクタを一切書いていない場合も同じ**（暗黙生成されるものがヘッダ扱いになる）。宣言を書いて `.cpp` に出すこと。

なぜ「あるファイルでは通るのに別のファイルでは落ちる」のかというと、たまたま他のヘッダ経由で完全型が見えている翻訳単位では成立してしまうため。**通っていても潜在的に壊れている**と考えてよい。

過去に踏んだ箇所: `Framework`（`AbstractSceneFactory`）、`FadeTransition`（`SpriteInstance`）、`SceneManager`（`Scene`）。

### `C3861: 識別子が見つかりません`

コードを別ファイルへ移したときに、無名 `namespace` のヘルパ関数が付いてこなかったケースが多い。移動元の無名 `namespace` を確認する。

### 新しく追加したファイルがビルドされない

`.vcxproj` と `.vcxproj.filters` の**両方**に追加が必要。エンジン側のファイルは `ArcanaEngine.vcxproj`、アプリ側は自分のプロジェクトへ。

---

## シェーダ

### `Release` だけ描画が崩れる / 行列が転置される

**`-Zpr`（行優先レイアウト）が揃っていない。** 実行時コンパイルと事前コンパイル（`compile_shaders.py`）で必ず同じにする。

### シェーダを直したのに反映されない

- `Debug` は実行時コンパイルなので**再実行するだけ**で反映される
- `Release` / `Development` は `.cso` を読むので**ビルドが必要**

`.hlsli` を直した場合は全シェーダが再コンパイルされる。

### `error.log` に `.cso が見つからないためフォールバック`

クック漏れか作業ディレクトリ違い。`py tools\Python\compile_shaders.py --force` を試す。

---

## アセット

### `.png` を置いたのに読めない

**`Resources/` に直接置いていないか。** 元データは `Assets/` に置く。ビルド時に `.dds` へ変換されて `Resources/` に出る。

### SceneEditor の一覧に出てこない

- ビルドしていない（変換が走っていない）
- **同名ファイルを上書きした** → ディレクトリの更新時刻が動かないので検知されない。`Rescan Models` ボタンを押す

### 変換ツールが落ちる

ファイル名・フォルダ名に**非 ASCII 文字**が入っていないか確認する。

### `Resources/` を編集したのに戻ってしまう

次のビルドで `Assets/` から再生成される。必ず `Assets/` 側を直す。

---

## 当たり判定

### コライダーを設定したのに当たらない

**`collider.enabled = true` を忘れている。** 既定は `false`。

その他の確認点。

- `shouldCollide` フックが `false` を返していないか
- 両方のエンティティが `CollisionSystem::Register()` されているか
- `CollisionSystem::GetInstance()->Update()` を毎フレーム呼んでいるか
- `GetEditableTranslate()` が `nullptr` を返していないか（位置が取れないと判定対象外）

### `shouldCollide` の挙動が不安定

**順序非依存に書く。** `(a,b)` と `(b,a)` で結果が変わってはいけない。低い方を first に正規化してから判定する。

---

## Git / チーム作業

### `.vcxproj` が編集できない（読み取り専用）

git-lfs の `lockable` 指定によるもの。編集前にロックを取る。

```
git lfs lock   ArcanaEngine/ArcanaEngine.vcxproj
git lfs unlock ArcanaEngine/ArcanaEngine.vcxproj
```

**push したら必ず解除する。** 放置すると他の人が作業できない。誰が何をロック中かは `git lfs locks` で分かる。

### 同じクラスに別々の関数を足しただけでコンフリクトする

**mergiraf をセットアップしていない。** `tools/mergiraf/setup_mergiraf.bat` をダブルクリック。merge driver の登録先である `.git/config` はバージョン管理されないので、**各自が1回ずつ**実行する必要がある。

### クローンしたらビルドが通らない

`.gitignore` が VS 標準テンプレート由来だと `Debug/` `lib/` `bin/` を「ビルド出力」として除外してしまう。このリポジトリではそれらが**ソースと外部ライブラリの実体**なので、打ち消しルールを入れてある。

新しくフォルダを足したときは、`git status` に出てくるか（＝追跡されるか）を確認する。

### `sync_engine.py diff` に差分が出続ける

改行コードの違いだけの可能性がある。現在の実装は BOM と改行を無視して比較するので、それでも出るなら本当の差分。

---

## パフォーマンス

### 起動が遅い

`Debug` は `.hlsl` を実行時コンパイルするので数秒かかる。`Development` / `Release` は `.cso` を読むので大幅に速い。

### どこが重いか分からない

`Development` 構成で ImGui の **Pepper** パネルを見る。区間ごとの CPU / GPU 時間が出る。計測したい範囲に `PEPPER_SCOPE("名前")` を足す（[11_Utilities.md](11_Utilities.md)）。

---

## 不具合を報告するとき

`Logs/<日時>/` を**フォルダごと**渡す。特に以下。

| ファイル | 内容 |
|---|---|
| `crash_stack.txt` | 関数名とソース行まで出る。まずこれ |
| `error.log` | エラーの詳細（パス・cwd など） |
| `session.log` | 起動時の KPI と診断 |
| `input.log` | `--replay` で再現できる |
| `crash.dmp` | VS で開くとクラッシュ時点の状態を見られる |

`Release` では `Error` 以上しかログに残らない点に注意（[11_Utilities.md](11_Utilities.md)）。
