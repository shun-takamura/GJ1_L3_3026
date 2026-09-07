"""ベルトコンベア用の tread(キャタピラ)テクスチャを手続き的に生成する。

設計:
    - 進行方向(U 軸)にシェブロン(山型 >>>)が一定周期で繰り返す、横方向タイル可能な模様。
      UV オフセットを U 方向へ流すだけで「ベルトが回っている」ように見える(スキニング不要)。
    - 縦(V 軸)はベルトの幅。端に薄い陰影を入れて金属の縁に見せる。
    - ライティングは焼き込まない(プリミティブは unlit)。色付けは StageGrid 側のティントに任せ、
      ここはグレー基調で出す。
    - period が width を割り切る値なので左右端がシームレスに繋がる。

使い方(Project/ で実行):
    python tools/Python/gen_belt_texture.py
    python tools/Python/gen_belt_texture.py --width 512 --repeats 8 --no-cook

出力:
    Assets/Textures/Belt_Tread.png   → cook_assets.py で Resources/Textures/Belt_Tread.dds へ
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def generate_tread(width: int, height: int, repeats: int,
                   slope: float, band: float) -> Image.Image:
    """width x height の tread 模様(RGB)。repeats = U 方向のシェブロン個数。"""
    period = width / repeats
    xs = np.arange(width, dtype=np.float32)[None, :]
    ys = np.arange(height, dtype=np.float32)[:, None]
    midv = (height - 1) * 0.5

    # シェブロンの位相: 中央から離れるほど後ろへずれた三角波
    phase = (xs - slope * np.abs(ys - midv)) % period
    phase_n = phase / period                      # 0..1

    # 暗い溝(帯) + 明るいベベル(帯の先端)
    groove = (phase_n < band).astype(np.float32)
    bevel = ((phase_n >= band) & (phase_n < band + 0.10)).astype(np.float32)

    base = np.full((height, width), 0.50, dtype=np.float32)   # 明るい半分(ティントで色が乗る)
    val = base + groove * (-0.34) + bevel * (0.38)            # 暗い溝をはっきり深く

    # ベルト幅方向の縁シェード
    edge = np.clip(np.abs(ys - midv) / midv, 0.0, 1.0)
    val -= (edge ** 3) * 0.12

    # ごく薄いノイズで平坦さを消す
    rng = np.random.default_rng(7)
    val += (rng.random((height, width)).astype(np.float32) - 0.5) * 0.03

    val = np.clip(val, 0.0, 1.0)
    rgb = np.stack([val, val, val * 0.98], axis=-1)           # ほんの少し寒色
    return Image.fromarray((rgb * 255.0 + 0.5).astype(np.uint8), mode="RGB")


def run_cooker(project_root: Path) -> None:
    cooker = project_root / "tools" / "Python" / "cook_assets.py"
    if not cooker.exists():
        print(f"[warn] cooker not found, skipped: {cooker}")
        return
    print(f"[cook] running {cooker.name} ...")
    result = subprocess.run([sys.executable, str(cooker)], cwd=str(project_root),
                            capture_output=True, text=True)
    if result.stdout:
        print(result.stdout.rstrip())
    if result.returncode != 0:
        print(result.stderr.rstrip())
        raise SystemExit(f"[error] cook_assets.py failed ({result.returncode})")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", type=str, default="Belt_Tread", help="出力ファイル名(拡張子なし)")
    ap.add_argument("--width", type=int, default=256, help="U 方向の解像度")
    ap.add_argument("--height", type=int, default=64, help="V 方向(ベルト幅)の解像度")
    ap.add_argument("--repeats", type=int, default=2, help="U 方向のシェブロン個数(width を割り切る値)")
    ap.add_argument("--slope", type=float, default=1.3, help="シェブロンの傾き(0=横縞)")
    ap.add_argument("--band", type=float, default=0.5, help="暗い溝の占める割合")
    ap.add_argument("--project-root", type=str, default=".", help="Project/ ディレクトリ(既定=カレント)")
    ap.add_argument("--no-cook", dest="cook", action="store_false",
                    help="PNG 生成のみ行い cook_assets.py を実行しない")
    ap.set_defaults(cook=True)
    args = ap.parse_args()

    project_root = Path(args.project_root).resolve()
    if not (project_root / "Assets").exists():
        raise SystemExit(f"Assets/ が見つかりません。Project/ で実行するか --project-root を指定してください: {project_root}")

    if args.width % args.repeats != 0:
        print(f"[warn] width({args.width}) が repeats({args.repeats}) で割り切れません。継ぎ目が出る可能性があります。")

    img = generate_tread(args.width, args.height, args.repeats, args.slope, args.band)

    out = project_root / "Assets" / "Textures" / f"{args.name}.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    print(f"[gen] wrote {out.relative_to(project_root)}  ({args.width}x{args.height}, {args.repeats} chevrons)")

    if args.cook:
        run_cooker(project_root)
    print(f"[done] StageGrid が Resources/Textures/{args.name}.dds を参照します。")


if __name__ == "__main__":
    main()
