"""ArcanaEngine をチーム配布用の zip に固める。

作られるものはソースを含まないバイナリ配布物で、受け取った側は
Template/ をコピーして自分のゲームを書き始められる。

    py tools\\Python\\package_engine.py            # 3構成そろっているか確認して zip 化
    py tools\\Python\\package_engine.py --version 0.1.0
    py tools\\Python\\package_engine.py --no-zip   # フォルダだけ作る（中身の確認用）

前提: Debug / Development / Release の3構成をビルド済みであること。
      .cso は Release / Development のビルド時に自動生成される。
"""
import argparse
import datetime
import os
import shutil
import sys
import zipfile
from pathlib import Path

# Project/ をカレントにして実行する前提
PROJECT = Path(".").resolve()
OUT_ROOT = Path("../Generated/EnginePackage")
CONFIGS = ["Debug", "Development", "Release"]
BUILD_DIR = Path("../Generated/Output")

# ヘッダを配るソースツリー（.cpp は含めない）
HEADER_ROOTS = [
    Path("DirectXGame/GameEngine"),
    Path("DirectXGame/ImGUIManager"),
    Path("DirectXGame/Debug"),
]
HEADER_SUFFIXES = {".h", ".hpp", ".inl"}

# まるごと同梱するディレクトリ（相対パスは維持される）
COPY_DIRS = [
    "Resources/CompiledShaders",   # 事前コンパイル済みシェーダ（.hlsl は入れない）
    "Resources/Textures",
    "Resources/Fonts",
    "Resources/Json/Effects",
    "externals",
    "tools",
    "Documents",
    "Assets",
]

# externals から外すもの（ビルド生成物・未使用のCRT版）
EXTERNALS_EXCLUDE = {
    "externals/Generated",
    "externals/assimp/lib/Debug/assimp-vc143-mdd.lib",
    "externals/assimp/lib/Release/assimp-vc143-md.lib",
}

COPY_FILES = ["Common.props"]

IGNORE = shutil.ignore_patterns("__pycache__", "*.pyc", ".vs", "*.user",
                                "sync_cache.json", "mergiraf.exe")


def excluded(rel_posix: str) -> bool:
    return any(rel_posix == e or rel_posix.startswith(e + "/")
               for e in EXTERNALS_EXCLUDE)


def copy_headers(dst_root: Path) -> int:
    n = 0
    for root in HEADER_ROOTS:
        if not root.is_dir():
            print("  [warn] 見つかりません: %s" % root)
            continue
        for src in root.rglob("*"):
            if not src.is_file() or src.suffix.lower() not in HEADER_SUFFIXES:
                continue
            dst = dst_root / src
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            n += 1
    return n


def copy_tree_filtered(src: Path, dst: Path) -> int:
    n = 0
    for f in src.rglob("*"):
        if not f.is_file():
            continue
        rel = f.as_posix()
        if excluded(rel):
            continue
        if any(part in ("__pycache__", ".vs") for part in f.parts):
            continue
        if f.name in ("sync_cache.json", "mergiraf.exe") or f.suffix == ".pyc":
            continue
        out = dst / f.relative_to(src)
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, out)
        n += 1
    return n


def newest_engine_source_mtime() -> float:
    """エンジンのソース（.cpp/.h）で一番新しい更新時刻。"""
    newest = 0.0
    for root in HEADER_ROOTS:
        if not root.is_dir():
            continue
        for f in root.rglob("*"):
            if f.is_file() and f.suffix.lower() in {".cpp", ".h", ".hpp", ".inl"}:
                newest = max(newest, f.stat().st_mtime)
    return newest


def check_libs_fresh() -> bool:
    """.lib がソースより古くないか確認する。

    package_engine.py はビルド済みの .lib をコピーするだけなので、
    エンジンを直したあとビルドを忘れると「直したはずの不具合が直っていない」
    配布物ができてしまう。ここで止める。
    """
    src_mtime = newest_engine_source_mtime()
    if src_mtime <= 0:
        return True

    import datetime as _dt
    stale = []
    for cfg in CONFIGS:
        lib = BUILD_DIR / cfg / "ArcanaEngine.lib"
        if lib.is_file() and lib.stat().st_mtime < src_mtime:
            stale.append((cfg, lib.stat().st_mtime))

    if not stale:
        return True

    fmt = lambda t: _dt.datetime.fromtimestamp(t).strftime("%m/%d %H:%M")
    print("[ERROR] エンジンのソースより古い .lib があります。", file=sys.stderr)
    print("        ソース最終更新: %s" % fmt(src_mtime), file=sys.stderr)
    for cfg, t in stale:
        print("        %-12s %s  ← 古い" % (cfg, fmt(t)), file=sys.stderr)
    print(file=sys.stderr)
    print("        該当構成をビルドし直してから再実行してください。", file=sys.stderr)
    print("        （どうしてもこのまま作るなら --allow-stale）", file=sys.stderr)
    return False


def collect_libs(dst_root: Path) -> bool:
    """3構成の .lib と Debug の .pdb を集める。1つでも欠けたら False。"""
    ok = True
    for cfg in CONFIGS:
        src_dir = BUILD_DIR / cfg
        lib = src_dir / "ArcanaEngine.lib"
        if not lib.is_file():
            print("  [ERROR] %s が見つかりません。%s 構成をビルドしてください" % (lib, cfg))
            ok = False
            continue

        out = dst_root / "lib" / cfg
        out.mkdir(parents=True, exist_ok=True)
        shutil.copy2(lib, out / lib.name)
        print("  lib/%-12s ArcanaEngine.lib  %6.1f MB" % (cfg, lib.stat().st_size / 1024 / 1024))

        # DirectXTex も同梱しないとチーム側がビルドできない
        dxtex = src_dir / "DirectXTex.lib"
        if dxtex.is_file():
            shutil.copy2(dxtex, out / dxtex.name)
        else:
            print("  [warn] %s が見つかりません" % dxtex)

        # Debug だけ .pdb も配る。これが無いとチームがクラッシュ時のスタックを追えない
        if cfg == "Debug":
            pdb = src_dir / "ArcanaEngine.pdb"
            if pdb.is_file():
                shutil.copy2(pdb, out / pdb.name)
                print("  lib/%-12s ArcanaEngine.pdb  %6.1f MB" % (cfg, pdb.stat().st_size / 1024 / 1024))
            else:
                print("  [warn] Debug の .pdb が見つかりません")

    # アセットのクックに使う実行ファイル
    gd = BUILD_DIR / "Release" / "gdeflate_compress.exe"
    if not gd.is_file():
        gd = BUILD_DIR / "Debug" / "gdeflate_compress.exe"
    if gd.is_file():
        bindir = dst_root / "bin"
        bindir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(gd, bindir / gd.name)
        # gdeflate_compress.exe は dstorage の DLL を要求する。
        # Windows は exe と同じフォルダを最初に探すので、bin/ にも置く
        for dll in ["dstorage.dll", "dstoragecore.dll"]:
            src = Path("externals/DirectStorage/bin/x64") / dll
            if src.is_file():
                shutil.copy2(src, bindir / dll)
            else:
                print("  [warn] %s が見つかりません" % src)
    else:
        print("  [warn] gdeflate_compress.exe が見つかりません（pack_assets が使えません）")

    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="ArcanaEngine の配布パッケージを作る")
    ap.add_argument("--version", default="", help="バージョン文字列（省略時は日付）")
    ap.add_argument("--no-zip", action="store_true", help="zip 化せずフォルダのまま残す")
    ap.add_argument("--allow-stale", action="store_true",
                    help="ソースより古い .lib でも作る（通常は使わない）")
    args = ap.parse_args()

    if not Path("Common.props").is_file():
        print("[ERROR] Project/ をカレントディレクトリにして実行してください", file=sys.stderr)
        return 1

    version = args.version or datetime.datetime.now().strftime("%Y-%m-%d")
    name = "ArcanaEngine-%s" % version

    pkg_root = (OUT_ROOT / name).resolve()
    if pkg_root.exists():
        shutil.rmtree(pkg_root)
    # リポジトリと同じ階層構造にする。エンジンは Resources/ を Project/ からの相対で読み、
    # 生成物は ../Generated/ へ出るため、この形でないとパスが合わない
    out_root = pkg_root / "Project"
    out_root.mkdir(parents=True)

    print("== %s を組み立てます" % name)
    print()

    if not args.allow_stale and not check_libs_fresh():
        return 1

    print("[1/5] ライブラリ")
    if not collect_libs(out_root):
        print()
        print("3構成すべてをビルドしてから再実行してください。", file=sys.stderr)
        return 1

    print("[2/5] 公開ヘッダ")
    n = copy_headers(out_root)
    print("  %d ファイル" % n)

    print("[3/5] リソースとツール")
    for d in COPY_DIRS:
        src = Path(d)
        if not src.is_dir():
            print("  [warn] 見つかりません: %s" % d)
            continue
        n = copy_tree_filtered(src, out_root / d)
        print("  %-28s %4d ファイル" % (d, n))
    for f in COPY_FILES:
        if Path(f).is_file():
            shutil.copy2(f, out_root / f)

    print("[4/5] テンプレート")
    tpl_src = Path("Template")
    if tpl_src.is_dir():
        n = copy_tree_filtered(tpl_src, out_root / "Template")
        print("  %d ファイル" % n)
    else:
        print("  [warn] Template/ がありません")

    # チームが開くソリューション。Template/MyGame.vcxproj を参照する
    if Path("MyGame.sln.template").is_file():
        shutil.copy2("MyGame.sln.template", out_root / "MyGame.sln")
    # 配布物用の README（リポジトリ用のものとは内容が違う）
    pkg_readme = Path("tools/package_readme.md")
    if pkg_readme.is_file():
        shutil.copy2(pkg_readme, pkg_root / "README.md")
    else:
        print("  [warn] tools/package_readme.md がありません")

    total = sum(1 for _ in pkg_root.rglob("*") if _.is_file())
    size = sum(f.stat().st_size for f in pkg_root.rglob("*") if f.is_file())
    print()
    print("  合計 %d ファイル / %.1f MB" % (total, size / 1024 / 1024))

    if args.no_zip:
        print()
        print("[DONE] %s" % pkg_root)
        return 0

    zip_path = pkg_root.parent / (name + ".zip")
    files = [f for f in sorted(pkg_root.rglob("*")) if f.is_file()]
    print("[5/5] zip 化 (%d ファイル。数分かかります)" % len(files))

    # 途中で止めても壊れた zip が残らないよう、一時名で書いてから差し替える
    tmp_path = zip_path.with_suffix(".zip.part")
    if tmp_path.exists():
        tmp_path.unlink()

    try:
        with zipfile.ZipFile(tmp_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for i, f in enumerate(files, 1):
                z.write(f, f.relative_to(pkg_root).as_posix())
                if i % 100 == 0 or i == len(files):
                    print("  %d / %d" % (i, len(files)), flush=True)
    except BaseException:
        if tmp_path.exists():
            tmp_path.unlink()
        raise

    if zip_path.exists():
        zip_path.unlink()
    tmp_path.replace(zip_path)

    print()
    print("[DONE] %s (%.1f MB)" % (zip_path, zip_path.stat().st_size / 1024 / 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
