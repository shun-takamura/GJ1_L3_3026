# 05. 入力

`InputManager` は `Framework` が初期化・毎フレーム更新まで面倒を見る。シーンからは `input_`（`Scene` の protected メンバ）で触れる。

デバイスは3種。

| 取得 | クラス |
|---|---|
| `input_->GetKeyboard()` | `KeyboardInput`（DirectInput） |
| `input_->GetMouse()` | `MouseInput`（DirectInput） |
| `input_->GetController()` | `ControllerInput`（XInput） |
| `input_->GetActionMap()` | `InputActionMap`（**推奨**） |

---

## 推奨: アクションマップを使う

デバイスを直接見るのではなく、**「ジャンプ」「射撃」といった意味に名前を付けて、そこに物理キーを割り当てる**方式。キーコンフィグ、ゲームパッド対応、リプレイがすべてこの層で解決する。

### 1. アクションを定義する

アプリ側で enum を用意する。エンジンは中身を知らない（ただの int として扱う）。

```cpp
// GameActions.h
enum class Action : int {
    MoveUp = 0,
    MoveDown,
    MoveLeft,
    MoveRight,
    Shoot,
    Dodge,
    Pause,
    Count
};
```

### 2. 初期化時にバインドする

各アクションは **2スロット**持てる（キーボードとゲームパッドを同時に割り当てられる）。

```cpp
#include "InputAction.h"
#include "PhysicalBinding.h"
#include <dinput.h>   // DIK_*

auto* map = input_->GetActionMap();
map->Resize(static_cast<size_t>(Action::Count));

auto bind = [&](Action a, size_t slot, const PhysicalBinding& b) {
    map->Bind(static_cast<int>(a), slot, b);
};

// スロット0 = キーボード/マウス
bind(Action::MoveUp,    0, PhysicalBinding::Keyboard(DIK_W));
bind(Action::MoveDown,  0, PhysicalBinding::Keyboard(DIK_S));
bind(Action::Shoot,     0, PhysicalBinding::Mouse(MouseCode::Left));
bind(Action::Pause,     0, PhysicalBinding::Keyboard(DIK_ESCAPE));

// スロット1 = ゲームパッド
bind(Action::MoveUp,    1, PhysicalBinding::Gamepad(XINPUT_GAMEPAD_DPAD_UP));
bind(Action::Shoot,     1, PhysicalBinding::Gamepad(GamepadCode::RT));
bind(Action::Dodge,     1, PhysicalBinding::Gamepad(XINPUT_GAMEPAD_B));
```

物理入力の指定方法は3つ。

```cpp
PhysicalBinding::Keyboard(DIK_SPACE)          // DirectInput のキーコード
PhysicalBinding::Mouse(MouseCode::Right)      // Left / Right / Middle / Button4
PhysicalBinding::Gamepad(XINPUT_GAMEPAD_A)    // XInput のビットマスク
PhysicalBinding::Gamepad(GamepadCode::LT)     // トリガーは独自コード（LT / RT）
```

### 3. 使う

```cpp
auto* map = input_->GetActionMap();

if (map->IsTriggered(static_cast<int>(Action::Shoot))) {
    // 押した瞬間だけ true
    FireBullet();
}
if (map->IsPressed(static_cast<int>(Action::MoveUp))) {
    // 押している間ずっと true
    position_.y += speed * dt;
}
if (map->IsReleased(static_cast<int>(Action::Dodge))) {
    // 離した瞬間だけ true
}
```

`AnyInputTriggered()` は「何か押されたか」の判定。タイトル画面の「Press Any Button」に使える。

### 保存と復元

`PhysicalBinding` は `"kb.Space"` `"mouse.Left"` `"gp.RT"` のような文字列に相互変換できる。キーコンフィグを JSON に保存するときに使う。

```cpp
std::string s = binding.ToString();
PhysicalBinding b = PhysicalBinding::FromString("gp.RT");   // 失敗時は Empty
```

---

## デバイスを直接使う

アナログスティックやマウス移動量など、アクションマップで表現しないものはこちら。

### キーボード

```cpp
auto* kb = input_->GetKeyboard();
if (kb->PuhsKey(DIK_LSHIFT))   { /* 押している間 */ }
if (kb->TriggerKey(DIK_SPACE)) { /* 押した瞬間 */ }
```

> `PuhsKey` は綴りが誤っているが既存 API なのでそのまま。

### マウス

```cpp
auto* mouse = input_->GetMouse();

// ボタン
if (mouse->IsButtonPressed(MouseInput::Button::Left))   { }
if (mouse->IsButtonTriggered(MouseInput::Button::Right)) { }
if (mouse->IsButtonReleased(MouseInput::Button::Middle)) { }

// 移動量（相対）— 視点操作向き
LONG dx = mouse->GetDeltaX();
LONG dy = mouse->GetDeltaY();
LONG wheel = mouse->GetDeltaWheel();

// 位置（絶対）
LONG cx = mouse->GetClientX();     // ウィンドウ内座標（UI 判定はこちら）
LONG cy = mouse->GetClientY();
LONG sx = mouse->GetScreenX();     // スクリーン座標
```

### ゲームパッド

```cpp
auto* pad = input_->GetController();
if (!pad->IsConnected()) return;

// ボタン
if (pad->IsButtonTriggered(XINPUT_GAMEPAD_A)) { }

// スティック（デッドゾーン処理済み）
auto ls = pad->GetLeftStick();
if (ls.magnitude > 0.0f) {
    velocity_.x += ls.x * speed * ls.magnitude;
    velocity_.z += ls.y * speed * ls.magnitude;
}

// トリガー（0.0 ~ 1.0）
float rt = pad->GetRightTrigger();
```

`Stick` は `x` / `y`（-1.0〜1.0 の方向）と `magnitude`（0.0〜1.0 の倒し具合）を持つ。**移動速度は `magnitude` を掛けて作る**と、軽く倒したときにゆっくり動く自然な挙動になる。

複数のコントローラが接続されていても、**最初に操作されたスロットを掴んで切断まで固定**する動作になっている。

---

## リプレイ

入力は自動的に記録され、`Logs/<日時>/input.log` に残る。再生できる。

```
Sample.exe --replay Logs/2026-09-01_154328
Sample.exe --seed 12345
```

`--replay` を指定するとその日のセッションの入力とシード（`session.log` から復元）で再生する。バグの再現に使う。

> **リプレイを壊さないために `rand()` を直接使わない。** 乱数は `RandomGenerator`（[11_Utilities.md](11_Utilities.md)）を通すこと。

---

## 注意

- `InputManager::Update()` は `Framework::Update()` が呼ぶ。**アプリから呼ばない**（2回更新すると Trigger 判定が壊れる）
- ImGui にフォーカスがある間もゲーム側の入力は生きている。テキスト入力中に操作を止めたいなら `ImGui::GetIO().WantCaptureKeyboard` を見て自前で抑制する
- `DIK_*` を使うには `<dinput.h>` の include が要る

---

## 関連

- 時間管理（ポーズ中に入力だけ生かす） → [11_Utilities.md](11_Utilities.md)
