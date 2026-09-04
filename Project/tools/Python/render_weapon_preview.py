"""生成した武器OBJを正面(横視点と同じ向き)から正投影でレンダリングして確認用PNGを出す、
使い捨てのデバッグ用スクリプト。パイプラインの一部ではない。

使い方:
    blender --background --python render_weapon_preview.py -- <入力.obj> <出力.png>
"""
import bpy
import math
import sys


def main():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    if len(argv) < 2:
        print("usage: render_weapon_preview.py -- <input.obj> <output.png>")
        sys.exit(1)
    in_path, out_path = argv[0], argv[1]

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)

    bpy.ops.wm.obj_import(filepath=in_path, forward_axis="NEGATIVE_Z", up_axis="Y")

    imported = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    for o in imported:
        o.select_set(True)
    bpy.context.view_layer.objects.active = imported[0] if imported else None

    # wm.obj_import も forward=-Z/up=Y でBlenderネイティブ(Z-up)へ逆変換するので、
    # インポート後は blender_Z = file_Y(上), blender_Y = -file_Z(奥行き) になる。
    # よって「横視点」= 奥行き軸(blender Y)に沿ってカメラを向け、
    # 画面の縦方向は blender Z(=見た目の上下)を使う。
    xs, ys, zs = [], [], []
    for o in imported:
        for v in o.bound_box:
            world = o.matrix_world @ __import__("mathutils").Vector(v)
            xs.append(world.x); ys.append(world.y); zs.append(world.z)
    cx, cz = (min(xs) + max(xs)) / 2, (min(zs) + max(zs)) / 2
    xspan = max(xs) - min(xs)
    zspan = max(zs) - min(zs)

    res_x, res_y = 900, 500
    # sensor_fit='HORIZONTAL' 固定: ortho_scaleは常に画面横幅ぶんの範囲を表し、
    # 縦の表示範囲は ortho_scale * (res_y/res_x) になる。アスペクト比の違う武器
    # (背が高い/低い)でも縦横どちらもフレームに収まるよう、必要な方を採用する。
    span = max(xspan, zspan * (res_x / res_y)) * 1.3

    cam_data = bpy.data.cameras.new("PreviewCam")
    cam_data.type = "ORTHO"
    cam_data.sensor_fit = "HORIZONTAL"
    cam_data.ortho_scale = span
    cam_obj = bpy.data.objects.new("PreviewCam", cam_data)
    bpy.context.scene.collection.objects.link(cam_obj)
    cam_obj.location = (cx, min(ys) - 5.0, cz)
    cam_obj.rotation_euler = (math.radians(90.0), 0.0, 0.0)  # +Y方向を見る(横視点と同じ向き)
    bpy.context.scene.camera = cam_obj

    light_data = bpy.data.lights.new("PreviewLight", type="SUN")
    light_data.energy = 3.0
    light_obj = bpy.data.objects.new("PreviewLight", light_data)
    bpy.context.scene.collection.objects.link(light_obj)
    light_obj.rotation_euler = (math.radians(45), math.radians(20), 0.0)

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = res_x
    scene.render.resolution_y = res_y
    scene.render.filepath = out_path
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"

    bpy.ops.render.render(write_still=True)
    print(f"[OK] rendered -> {out_path}")


main()
