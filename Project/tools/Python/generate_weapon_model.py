"""Blender (bpy) 武器モデル生成スクリプト。

既存の Pistol/Shotgun/AssaultRifle と同じ流儀(直方体+円柱+トーラスの組み合わせを
1オブジェクトに結合したロウポリ・プロップ、単一マテリアル、Blenderの新OBJエクスポータで
書き出し)で武器モデルを手続き的に生成する。Blender GUIで一から作る代わりに、
このスクリプトを编集して寸法を調整 → 再実行、のサイクルで素早く反復できるようにする狙い。
今後追加予定の武器(チャージキャンナン/ロケットランチャー/マイン等、weapon_design メモ参照)も
WEAPON_BUILDERS に関数を足すだけで同じパイプラインに乗る。

2026-09-04 追記: 初回実装(Blaster/GrenadeLauncherのみ)は「見てすぐこの武器だとわかりづらい」
というユーザーの指摘を受けて全5種を再設計。各武器のシルエットを実銃の分かりやすい記号
(拳銃=小型、ポンプアクション式ショットガン=前方スライド式フォアグリップ、
アサルトライフル=下向きマガジン+ストック、ブラスター=ラッパ状の爆風銃口、
グレネードランチャー=単一極太チューブ+薬室バルジ)で差別化し、さらにマテリアルの色も
武器ごとに変えて「一目で見分けられる」ことを優先した。ブラスターの橙は
weapon_log.html の "爆風のデバッグ表示色と同じ橙" というコメントに合わせた意図的な選択。

座標系: +X = 銃口方向(前)、+Y = 上、Z = 奥行き(薄い軸)。ゲーム側は横視点の2.5Dなので
Z方向は常に薄く保つ。原点(0,0,0)がグリップ付近、キャラクターの手の位置に来る想定。

使い方 (Blender 4.4):
    "C:\\Program Files\\Blender Foundation\\Blender 4.4\\blender.exe" --background ^
        --python generate_weapon_model.py -- <WeaponName> <出力先.obj>

.mtl は Ns/Ka/Ks/Ni/illum をチーム既存アセットと揃えつつ、Kd(拡散色)だけ
WEAPON_KD の値で武器ごとに変えたものをこのスクリプト自身が書き出す
(Blenderの既定マテリアルのエクスポートには頼らない ── バージョンによって
Ns等の値やPBR拡張フィールドの有無が変わり得るため)。
"""

import bpy
import math
import os
import sys


# --- 座標系メモ ---
# このスクリプトの公開座標系(build_*関数が使う数値)は常に (x=前方, y=上, z=奥行き)。
# ところが Blender 内部は Z-up(奥行き視点ではなくトップビュー基準)で、
# wm.obj_export の forward_axis='NEGATIVE_Z' / up_axis='Y' は書き出し時に
#   file_X = blender_X, file_Y = blender_Z, file_Z = -blender_Y
# という変換をかける(既存Pistol等のexportと合わせるためこの設定は固定)。
# よって「前方,上,奥行き」で考えた座標を Blender に渡す前に、この変換の逆
#   blender_X = x, blender_Y = -z, blender_Z = y
# を通す必要がある。以下のヘルパー関数の中でだけこの変換を行い、
# build_*() 側は素直に (前方,上,奥行き) で書けるようにしてある。
def _to_blender(x: float, y_up: float, z_depth: float):
    return (x, -z_depth, y_up)


def add_box(center, size, name, rot_z_deg: float = 0.0):
    """center/size は (前方,上,奥行き) 系での中心と全長(半径ではない)。
    rot_z_deg は前方-上平面(見た目の画面)内で中心周りに回す角度(度)。"""
    bx, by, bz = _to_blender(*center)
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(bx, by, bz))
    obj = bpy.context.active_object
    obj.name = name
    sx, sy, sz = size
    obj.scale = (sx, sz, sy)  # 奥行き(z)→Blender Y、上(y)→Blender Z
    if rot_z_deg:
        # 見た目の「前方-上」平面内での回転は、Blenderのローカル系ではY軸周りの回転になる
        # (file_Z軸 = -blender_Y軸 なので回転軸自体はblender Yに一致する)。
        obj.rotation_euler[1] = math.radians(rot_z_deg)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return obj


def add_cylinder_x(x0: float, x1: float, radius: float, y: float = 0.0, z: float = 0.0,
                    verts: int = 10, name: str = "Cyl"):
    """(前方,上,奥行き)系で x0→x1(前方軸)に沿って伸びる円柱。"""
    depth = x1 - x0
    cx = (x0 + x1) * 0.5
    bx, by, bz = _to_blender(cx, y, z)
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=verts, radius=radius, depth=depth,
        location=(bx, by, bz), rotation=(0.0, math.radians(90.0), 0.0))
    obj = bpy.context.active_object
    obj.name = name
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return obj


def add_cone_x(x0: float, x1: float, r0: float, r1: float, y: float = 0.0, z: float = 0.0,
               verts: int = 10, name: str = "Cone"):
    """(前方,上,奥行き)系で x0(半径r0)→x1(半径r1)へテーパーする円錐台。ラッパ銃口のベル/フレア用。"""
    depth = x1 - x0
    cx = (x0 + x1) * 0.5
    bx, by, bz = _to_blender(cx, y, z)
    bpy.ops.mesh.primitive_cone_add(
        vertices=verts, radius1=r0, radius2=r1, depth=depth,
        location=(bx, by, bz), rotation=(0.0, math.radians(90.0), 0.0))
    obj = bpy.context.active_object
    obj.name = name
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return obj


def add_ring(x: float, y: float, major_r: float, minor_r: float, name: str = "Guard"):
    """トリガーガード用のリング。輪の面が前方-上平面(見た目の画面)に来るよう、
    既定のトーラス(穴の軸=Blender Z)をX軸周りに90度回して穴の軸をBlender Yへ倒す
    (file_Z軸 = -blender_Y軸 なので、これで輪が奥行き軸から見て正面向きになる)。"""
    bx, by, bz = _to_blender(x, y, 0.0)
    bpy.ops.mesh.primitive_torus_add(
        location=(bx, by, bz), rotation=(math.radians(90.0), 0.0, 0.0),
        major_radius=major_r, minor_radius=minor_r,
        major_segments=16, minor_segments=8)
    obj = bpy.context.active_object
    obj.name = name
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return obj


def build_pistol():
    """単発拳銃。他の全武器より一回り小さい・シンプルというのが最大の識別要素
    (マガジン・ストック・フォアグリップの類を一切持たない、素の小型サイドアーム)。"""
    parts = [
        add_box((0.05, 0.045, 0.0), (0.30, 0.09, 0.10), "Slide"),
        add_cylinder_x(0.20, 0.30, 0.035, y=0.045, verts=8, name="Barrel"),
        add_box((-0.06, -0.14, 0.0), (0.10, 0.26, 0.09), "Grip", rot_z_deg=-16.0),
        add_ring(0.02, -0.02, 0.045, 0.009, "Guard"),
    ]
    return parts, "Pistol"


def build_shotgun():
    """ポンプアクション式散弾銃。フレーム前方から間隔を空けて浮いた
    「ポンプ用フォアグリップ+マガジンチューブ(主砲身の下に並走する細いチューブ)」が
    最大の識別要素 ── 他のどの武器にも無い、ショットガン特有のシルエット。
    ストックは持たせず(タクティカル寄りの拳銃グリップのみ)、AssaultRifleと被らないようにする。"""
    parts = [
        add_box((0.05, 0.06, 0.0), (0.50, 0.14, 0.14), "Receiver"),
        add_cylinder_x(0.28, 0.85, 0.05, y=0.075, verts=10, name="Barrel"),
        add_cylinder_x(0.30, 0.75, 0.028, y=0.015, verts=8, name="MagTube"),
        add_box((0.50, 0.02, 0.0), (0.16, 0.065, 0.12), "PumpGrip"),
        add_box((-0.10, -0.18, 0.0), (0.13, 0.38, 0.15), "Grip", rot_z_deg=-14.0),
        add_ring(0.0, -0.045, 0.07, 0.012, "Guard"),
    ]
    return parts, "Shotgun"


def build_assault_rifle():
    """連射式ライフル。下向きのマガジン(前傾させて曲がった弾倉らしく見せる)+ストック+
    前方ハンドガードが識別要素。ショットガンのポンプチューブや、
    グレネードランチャーの極太単一チューブとは明確に違うシルエットになる。"""
    parts = [
        add_box((0.025, 0.07, 0.0), (0.65, 0.16, 0.11), "Receiver"),
        add_cylinder_x(0.33, 0.95, 0.032, y=0.075, verts=8, name="Barrel"),
        add_box((0.45, 0.0725, 0.0), (0.30, 0.085, 0.10), "Handguard"),
        add_box((0.05, -0.15, 0.0), (0.10, 0.30, 0.07), "Magazine", rot_z_deg=-12.0),
        add_box((-0.10, -0.20, 0.0), (0.12, 0.40, 0.14), "Grip", rot_z_deg=-14.0),
        add_ring(-0.02, -0.045, 0.065, 0.011, "Guard"),
        # Stockはレシーバー(高さ-0.01〜0.15)より明確に低い位置に置き、上端に段差をつけて
        # 「後ろに続く別パーツ」だと分かるようにする(高さを揃えると受信機と一体化して見えてしまう)。
        add_box((-0.525, 0.035, 0.0), (0.45, 0.13, 0.09), "Stock"),
        add_box((-0.775, 0.04, 0.0), (0.05, 0.18, 0.13), "ButtPlate"),
        add_box((0.0, 0.17, 0.0), (0.10, 0.04, 0.03), "RearSight"),
    ]
    return parts, "AssaultRifle"


def build_blaster():
    """至近距離爆風砲。短くて口径の太いブランダーバス/コンカッションキャノン風。
    Blaster.h の設計コメント(反動大・至近距離特化)に見た目を合わせ、
    他の銃より寸胴で銃口が朝顔状にラッパ状(フレア)に開く。
    上面の2本のリブ(HeatVent)は「発射熱を逃がす爆風砲」を連想させるための追加装飾。"""
    parts = [
        add_box((0.04, 0.06, 0.0), (0.52, 0.16, 0.18), "Frame"),
        add_cylinder_x(0.26, 0.55, 0.055, y=0.075, verts=10, name="Bore"),
        add_cone_x(0.55, 0.68, 0.055, 0.14, y=0.075, verts=10, name="Bell"),
        add_box((-0.12, -0.19, 0.0), (0.14, 0.42, 0.16), "Grip", rot_z_deg=-14.0),
        add_ring(0.02, -0.05, 0.075, 0.013, "Guard"),
        add_box((-0.07, 0.16, 0.0), (0.08, 0.04, 0.05), "Sight"),
        add_box((-0.05, 0.145, 0.05), (0.30, 0.02, 0.02), "HeatVentL"),
        add_box((-0.05, 0.145, -0.05), (0.30, 0.02, 0.02), "HeatVentR"),
    ]
    return parts, "Blaster"


def build_grenade_launcher():
    """山なりに跳ね返る榴弾を撃つ、単一の太い筒を持つランチャー。
    受け口(Receiver)とバレルの間にある薬室バルジ(Breech、太い円柱)が
    「何か大きな弾を装填している」ことを示す識別要素で、
    AssaultRifleの細い銃身+マガジンとも、Shotgunのポンプチューブとも被らない。"""
    parts = [
        add_box((-0.05, 0.065, 0.0), (0.50, 0.17, 0.15), "Receiver"),
        add_cylinder_x(0.18, 0.30, 0.10, y=0.075, verts=10, name="Breech"),
        add_cylinder_x(0.30, 1.00, 0.075, y=0.075, verts=10, name="Barrel"),
        add_cone_x(1.00, 1.05, 0.075, 0.09, y=0.075, verts=10, name="MuzzleCap"),
        add_box((0.03, 0.175, 0.0), (0.26, 0.045, 0.035), "Rail"),
        add_box((-0.10, -0.185, 0.0), (0.13, 0.40, 0.15), "Grip", rot_z_deg=-14.0),
        add_ring(0.0, -0.05, 0.07, 0.011, "Guard"),
        add_box((-0.575, 0.035, 0.0), (0.45, 0.13, 0.09), "Stock"),
        add_box((-0.825, 0.04, 0.0), (0.05, 0.18, 0.13), "ButtPlate"),
    ]
    return parts, "GrenadeLauncher"


def build_sniper_rifle():
    """最速弾・ワンパン級・低連射のライフル。全銃中もっとも長い(バレルが極端に長く細い)+
    上部のスコープが最大の識別要素。AssaultRifleと違いマガジンは持たせず、
    ストックも(ARの太い曲面ストックと違い)細く直線的にしてボルトアクション然とした印象にする。"""
    parts = [
        add_box((-0.025, 0.06, 0.0), (0.65, 0.12, 0.09), "Receiver"),
        add_cylinder_x(0.28, 1.35, 0.025, y=0.06, verts=8, name="Barrel"),
        add_box((0.15, 0.135, 0.0), (0.40, 0.03, 0.04), "Rail"),
        add_cylinder_x(0.05, 0.30, 0.035, y=0.185, verts=8, name="Scope"),
        add_cone_x(0.02, 0.05, 0.020, 0.045, y=0.185, verts=8, name="ScopeObjective"),
        add_box((-0.05, -0.15, 0.0), (0.10, 0.30, 0.08), "Grip", rot_z_deg=-14.0),
        add_ring(0.0, -0.02, 0.055, 0.009, "Guard"),
        add_box((-0.55, 0.055, 0.0), (0.40, 0.09, 0.07), "Stock"),
        add_box((-0.775, 0.055, 0.0), (0.05, 0.13, 0.10), "ButtPlate"),
    ]
    return parts, "SniperRifle"


def build_minigun():
    """最大連射・最低威力の重機関銃。5本のバレルを円状に束ねたガトリング式の砲身が
    唯一無二の識別要素(単一バレルの他武器とは絶対に混同しない)。
    フレーム下のアモボックス(側面給弾箱)も追加の識別要素。ストックは持たせず、
    「両手で抱える重火器」感を出す。"""
    parts = [
        add_box((0.075, 0.06, 0.0), (0.45, 0.16, 0.18), "Hub"),
    ]
    barrel_count = 5
    ring_r = 0.055
    barrel_r = 0.022
    for i in range(barrel_count):
        theta = math.radians(90.0 + i * (360.0 / barrel_count))
        by = 0.075 + ring_r * math.cos(theta)
        bz = ring_r * math.sin(theta)
        parts.append(add_cylinder_x(0.28, 0.75, barrel_r, y=by, z=bz, verts=8, name=f"Barrel{i}"))
    parts += [
        add_box((-0.05, -0.145, 0.0), (0.20, 0.19, 0.16), "AmmoBox"),
        add_box((-0.28, -0.16, 0.0), (0.11, 0.28, 0.10), "Grip", rot_z_deg=-14.0),
        add_ring(-0.20, -0.03, 0.05, 0.009, "Guard"),
    ]
    return parts, "Minigun"


def build_hand_cannon():
    """弾数1〜2発・全銃中最大ノックバックの緊急/フィニッシャー武器。
    フレームとバレルの間に挟んだ極太のシリンダー(リボルバーの弾倉)が最大の識別要素 ──
    Pistolには無いパーツで、かつBlasterの銃口フレアとも違う「途中の膨らみ」なので混同しない。
    バレル自体も短く極太にして「威力全部乗せ」の見た目にする。"""
    parts = [
        add_box((-0.005, 0.06, 0.0), (0.31, 0.12, 0.14), "Frame"),
        add_cylinder_x(0.13, 0.24, 0.095, y=0.065, verts=8, name="Cylinder"),
        add_cylinder_x(0.24, 0.42, 0.045, y=0.065, verts=8, name="Barrel"),
        add_box((-0.14, -0.19, 0.0), (0.15, 0.36, 0.15), "Grip", rot_z_deg=-18.0),
        add_ring(-0.02, -0.02, 0.06, 0.012, "Guard"),
        add_box((-0.10, 0.135, 0.0), (0.05, 0.05, 0.03), "Hammer"),
    ]
    return parts, "HandCannon"


WEAPON_BUILDERS = {
    "Pistol": build_pistol,
    "Shotgun": build_shotgun,
    "AssaultRifle": build_assault_rifle,
    "Blaster": build_blaster,
    "GrenadeLauncher": build_grenade_launcher,
    "SniperRifle": build_sniper_rifle,
    "Minigun": build_minigun,
    "HandCannon": build_hand_cannon,
}

# newmtl の Kd(拡散色)。他は既存踏襲のニュートラルグレーのままだが、
# 5種を一目で見分けられるよう武器ごとに変える。Blasterの橙は weapon_log.html の
# 「爆風(Blaster)のデバッグ表示色と同じ橙」というコメントに合わせた意図的な選択。
WEAPON_KD = {
    "Pistol": (0.22, 0.22, 0.24),          # 黒に近いガンメタル。最小・最シンプルな護身用サイドアーム
    "Shotgun": (0.58, 0.45, 0.30),         # 木製ストック/フォアグリップを連想させる褐色
    "AssaultRifle": (0.30, 0.33, 0.28),    # タクティカルなオリーブ寄りグレー
    "Blaster": (0.85, 0.38, 0.12),         # 爆風エフェクトと同じ橙(GameScene::ResolveExplosionのデバッグフラッシュ色)
    "GrenadeLauncher": (0.40, 0.42, 0.33), # 榴弾ランチャーらしいオリーブドラブ
    "SniperRifle": (0.22, 0.26, 0.32),     # 冷たい青みがかったガンメタル。精密狙撃のイメージ
    "Minigun": (0.45, 0.38, 0.22),         # 薬莢の真鍮を連想させるカーキ/ブラス
    "HandCannon": (0.70, 0.70, 0.74),      # 明るいクロームシルバー。マグナムらしい派手さ・全銃中最も明るい色
}


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for datablocks in (bpy.data.meshes, bpy.data.materials, bpy.data.cameras, bpy.data.lights):
        for block in list(datablocks):
            if block.users == 0:
                datablocks.remove(block)


def write_mtl(mtl_path: str, mat_name: str, kd) -> None:
    """チーム既存アセット(Pistol.mtl等)と同じテンプレートで.mtlを書き出す。
    KdだけWEAPON_KDの値、それ以外(Ns/Ka/Ks/Ni/illum)は既存踏襲の固定値にする。"""
    with open(mtl_path, "w", encoding="utf-8") as f:
        f.write("# Blender 4.4.1 MTL File: 'None'\n")
        f.write("# www.blender.org\n\n")
        f.write(f"newmtl {mat_name}\n")
        f.write("Ns 250.000000\n")
        f.write("Ka 1.000000 1.000000 1.000000\n")
        f.write(f"Kd {kd[0]:.6f} {kd[1]:.6f} {kd[2]:.6f}\n")
        f.write("Ks 0.500000 0.500000 0.500000\n")
        f.write("Ke 0.000000 0.000000 0.000000\n")
        f.write("Ni 1.500000\n")
        f.write("d 1.000000\n")
        f.write("illum 2\n")


def main() -> None:
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    if len(argv) < 2:
        print("usage: blender --background --python generate_weapon_model.py -- <WeaponName> <output.obj>")
        sys.exit(1)

    weapon_name, out_path = argv[0], argv[1]
    if weapon_name not in WEAPON_BUILDERS:
        print(f"[ERROR] unknown weapon: {weapon_name} (known: {list(WEAPON_BUILDERS)})")
        sys.exit(1)

    clear_scene()
    parts, obj_name = WEAPON_BUILDERS[weapon_name]()

    bpy.ops.object.select_all(action="DESELECT")
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()

    joined = bpy.context.active_object
    joined.name = obj_name
    joined.data.name = obj_name

    mat_name = f"{obj_name}Material"
    mat = bpy.data.materials.new(name=mat_name)
    joined.data.materials.append(mat)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.wm.obj_export(
        filepath=out_path,
        export_selected_objects=False,
        forward_axis="NEGATIVE_Z",
        up_axis="Y",
        export_materials=True,
        export_triangulated_mesh=True,
        export_uv=True,
        export_normals=True,
    )

    mtl_path = os.path.splitext(out_path)[0] + ".mtl"
    write_mtl(mtl_path, mat_name, WEAPON_KD[weapon_name])

    print(f"[OK] exported {weapon_name} -> {out_path} (+ {mtl_path})")


main()
