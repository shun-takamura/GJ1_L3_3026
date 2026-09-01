# 06. 当たり判定

構成は3層。**「当たったか」はエンジン、「誰と誰が当たるか」はゲーム**という分担になっている。

| 層 | 役割 |
|---|---|
| `CollisionGeometry` | 球 / OBB / カプセルの交差判定。状態を持たない純粋な幾何演算 |
| `CollisionSystem` | 登録・総当たり判定・デバッグ描画。シングルトン |
| `Collider` | 形状データ |

レイヤー（＝どのタグ同士が当たるか）のルールは**フックでゲーム側から注入する**。エンジンはレイヤーを「ただの整数」としか見ない。

---

## 最小の使い方

### 1. 起動時にルールを配線する

```cpp
#include "Physics/CollisionSystem.h"

enum class Tag : int { None = 0, Player, PlayerBullet, Enemy, Terrain, Count };

CollisionHostHooks hooks{};

// エンティティ → レイヤー番号
hooks.getLayer = [](IImGuiEditable* e) {
    return static_cast<int>(MyGame::TagOf(e));
};

// このレイヤーの組み合わせは判定するか（順序非依存で実装すること）
hooks.shouldCollide = [](int a, int b) {
    if (a > b) std::swap(a, b);
    // 自機と自機弾は当たらない
    if (a == (int)Tag::Player && b == (int)Tag::PlayerBullet) return false;
    if (a == (int)Tag::Enemy  && b == (int)Tag::Enemy)        return false;
    return true;
};

// 衝突が成立したときの処理（ダメージ適用など）
hooks.onHit = [](IImGuiEditable* a, IImGuiEditable* b) {
    MyGame::ExchangeDamage(a, b);
};

// デバッグ描画のレイヤー別カラー（省略可。未配線なら緑一色）
hooks.getLayerColor = [](int layer, float& r, float& g, float& b, float& a) {
    MyGame::GetTagColor(static_cast<Tag>(layer), r, g, b, a);
};

CollisionSystem::SetHostHooks(hooks);
```

**すべて未配線でも動く**（全ペア判定 + `onCollision` コールバックのみ）。まず動かしたいときはフックなしで始めてよい。

### 2. エンティティを登録してコライダーを設定する

登録は `IImGuiEditable::SetHooks` に仕込んでおくと自動になる（[01_GettingStarted.md](01_GettingStarted.md)）。

```cpp
auto* cs = CollisionSystem::GetInstance();
cs->Register(entity);                 // 通常は SetHooks 経由で自動

Collider& c = cs->ColliderOf(entity);
c.shape   = ColliderShape::Sphere;
c.radius  = 1.5f;
c.offset  = { 0.0f, 1.0f, 0.0f };     // オーナーの translate からのローカルオフセット
c.enabled = true;                     // ★これを true にしないと判定されない
c.showDebug = true;

// 衝突したときのコールバック（相手を受け取る）
c.onCollision = [this](IImGuiEditable* other) {
    OnHitSomething(other);
};
```

### 3. 毎フレーム更新する

```cpp
void MyScene::Update() {
    // ... 移動処理の後に呼ぶ
    CollisionSystem::GetInstance()->Update();
}
```

`Update()` は Debug ビルドでは末尾に `DrawDebug()` も呼ぶ。

### 4. 破棄

```cpp
cs->Unregister(entity);   // 通常は SetHooks 経由で自動
cs->Clear();              // シーン切り替え時に全消去
```

コライダーは**エンティティのポインタをキーにしたサイドテーブル**で保持されるので、エンティティ基底クラスに手を入れる必要はない。

---

## 形状

```cpp
enum class ColliderShape : int { Sphere = 0, OBB = 1, Capsule = 2 };
```

| 形状 | 使うフィールド |
|---|---|
| `Sphere` | `radius` |
| `OBB` | `halfExtents`（ローカル X/Y/Z の半幅） |
| `Capsule` | `capsuleRadius`, `capsuleHeight`（円柱部分のみ。両端の半球は含まない。ローカル Y 軸沿い） |

向きはオーナーの `GetEditableRotate()` から取る。回転を持たないエンティティは軸整列になる。

全9通りの組み合わせが実装済み。ただし **OBB × Capsule だけは近似**（カプセルの線分を6分割して Sphere-OBB 判定）。

---

## Collider のフィールド

```cpp
struct Collider {
    bool  enabled = false;                 // これが false なら判定しない
    bool  showDebug = true;                // デバッグ描画に出すか
    ColliderShape shape = ColliderShape::Sphere;
    Vector3 offset{ 0, 0, 0 };

    float   radius = 1.0f;                 // Sphere
    Vector3 halfExtents{ 0.5f, 0.5f, 0.5f }; // OBB
    float   capsuleRadius = 0.5f;          // Capsule
    float   capsuleHeight = 1.0f;

    std::function<void(IImGuiEditable* other)> onCollision;

    bool isCollidingThisFrame = false;     // 毎フレーム更新される。押しっぱなし判定にも使える
};
```

`isCollidingThisFrame` はデバッグ描画で「衝突中は赤くする」判定に使われるほか、ゲーム側から「今フレーム当たっているか」を見るのにも使える。

---

## デバッグ描画

`CollisionSystem::Update()` が Debug ビルドで自動的に呼ぶ。ImGui の Collision パネル（アプリ側で登録している場合）や、コードから一括で切れる。

```cpp
CollisionSystem::GetInstance()->SetDrawDebugEnabled(false);
```

- 衝突していない → レイヤーの色（`getLayerColor` 未配線なら緑）
- 衝突中 → 赤

個別の `collider.showDebug` が true でも、この全体スイッチが false なら描画されない。

---

## 交差判定を直接使う

独自のブロードフェーズを組みたい、レイキャストしたいなど、`CollisionSystem` の総当たりに乗らないケース。

```cpp
#include "Physics/CollisionGeometry.h"

// エンティティの姿勢をワールドに展開
CollisionGeometry::WorldData wa, wb;
if (CollisionGeometry::TryGetWorldData(entityA, colliderA, wa) &&
    CollisionGeometry::TryGetWorldData(entityB, colliderB, wb)) {
    if (CollisionGeometry::TestPair(colliderA, wa, colliderB, wb)) {
        // 当たった
    }
}
```

低レベル関数も公開されている。

```cpp
bool TestSphereSphere(const Vector3& ca, float ra, const Vector3& cb, float rb);
bool TestSphereOBB(const Vector3& sc, float sr, const Vector3& oc, const Vector3 axes[3], const Vector3& he);
bool TestSphereCapsule(const Vector3& sc, float sr, const Vector3& cc, const Vector3 axes[3], float h, float cr);
bool TestCapsuleCapsule(...);
bool TestOBBOBB(...);      // SAT による標準15軸テスト
bool TestOBBCapsule(...);  // 近似

Vector3 ClosestPointOnOBB(const Vector3& p, const Vector3& center, const Vector3 axes[3], const Vector3& he);
Vector3 ClosestPointOnSegment(const Vector3& a, const Vector3& b, const Vector3& p);
float   SegmentSegmentDistSq(const Vector3& p1, const Vector3& q1, const Vector3& p2, const Vector3& q2);
```

`CollisionGeometry` は状態を持たないので、どこから呼んでも安全。

---

## Inspector での編集

コライダーは Inspector の `Collider` 欄で編集できる（形状・オフセット・半径・デバッグ表示）。`translate` を持つエンティティなら表示される。

編集対象の実体は `ImGuiManager` の `getCollider` フックで指定できる。ゲームが独自のコンポーネント表にコライダーを持っている場合はそこを返す。未配線なら `CollisionSystem` のサイドテーブルが使われる（[10_Editor.md](10_Editor.md)）。

---

## 注意

- **`enabled = true` を忘れない。** 既定は `false`。「コライダーを設定したのに当たらない」の大半はこれ
- `shouldCollide` は**順序非依存**に書く。`(a,b)` と `(b,a)` で結果が変わると挙動が不安定になる
- 総当たり（O(n²)）なので、エンティティが数千を超えるならブロードフェーズを自前で組む
- `onHit` フックと `collider.onCollision` は両方呼ばれる。前者はゲーム共通処理、後者は個体固有の反応、という使い分けを想定している

---

## 関連

- デバッグ描画の他の関数 → [11_Utilities.md](11_Utilities.md)
- フック全体像 → [10_Editor.md](10_Editor.md)
