"""Render the animated scenes on the PC, no board needed.

Compiles the real scene code (src/aquarium.cpp, src/corgis.cpp) with Visual C++
against a stand-in display (tools/preview/), runs it, and saves a contact sheet
of frames as a PNG. Text in font 2 (the clock) is approximate.

Usage:
  py -3.12 tools/preview.py corgis --season t --time 10:30 --frames 20,60,100,140 \
      --tap 30:110:205 --scale 2 --out corgis.png
  py -3.12 tools/preview.py fish --season c

  --season a|n|t|c|N auto (from --date) / none / thanksgiving / christmas, or a number from src/season.h
  --weather CODE:TEMP real weather for the corgi yard (WMO code, degrees F), e.g. 61:50 for rain
  --tap F:X:Y        tap the screen at (X, Y) on frame F (repeatable)
Frames run at ~22 fps, so frame 22 is about one second in.
"""
import argparse
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from screenshot import rgb565_rows, write_png  # noqa: E402

OUT = ROOT / ".pio" / "preview"
SOURCES = ["tools/preview/harness.cpp", "src/aquarium.cpp", "src/corgis.cpp", "src/season.cpp", "src/scene_common.cpp"]


def vcvars():
    vswhere = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    path = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
                           "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                          capture_output=True, text=True).stdout.strip()
    bat = pathlib.Path(path) / "VC/Auxiliary/Build/vcvars64.bat"
    if not path or not bat.exists():
        sys.exit("Visual C++ build tools not found")
    return bat


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "preview.exe"
    inputs = [ROOT / s for s in SOURCES] + list((ROOT / "src").glob("*.h")) + list((ROOT / "tools/preview").glob("*"))
    if exe.exists() and all(p.stat().st_mtime < exe.stat().st_mtime for p in inputs):
        return exe
    font_dir = ROOT / ".pio/libdeps/cyd/TFT_eSPI/Fonts"
    if not font_dir.exists():
        sys.exit("Run a normal PlatformIO build once first (it downloads TFT_eSPI)")
    # A .bat file sidesteps cmd.exe's quoting rules for paths with spaces
    bat = OUT / "build.bat"
    srcs = " ".join(s.replace("/", "\\") for s in SOURCES)
    bat.write_text(
        "@echo off\r\n"
        f'call "{vcvars()}" >nul || exit /b 1\r\n'
        f'cl /nologo /O2 /EHsc /std:c++17 /W1 /wd4244 /wd4305 /I tools\\preview /I src /I "{font_dir}" '
        f'{srcs} /Fe:"{exe}" /Fo:"{OUT}\\\\"\r\n')
    r = subprocess.run(["cmd", "/c", str(bat)], cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-6000:], r.stderr[-2000:])
        sys.exit("preview build failed")
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scene", choices=["fish", "corgis"])
    ap.add_argument("--season", default="n")
    ap.add_argument("--date", default="2026-11-12")
    ap.add_argument("--time", default="12:00")
    ap.add_argument("--seed", default="1")
    ap.add_argument("--frames", default="30,90")
    ap.add_argument("--tap", action="append", default=[])
    ap.add_argument("--weather", default=None)
    ap.add_argument("--scale", type=int, default=1)
    ap.add_argument("--cols", type=int, default=2)
    ap.add_argument("--out", default=str(OUT / "preview.png"))
    a = ap.parse_args()

    exe = build()
    prefix = OUT / f"{a.scene}"
    frames = [int(f) for f in a.frames.split(",")]
    args = [str(exe), a.scene, a.season, a.date, a.time, a.seed, str(prefix), a.frames] + [f"tap={t}" for t in a.tap] + ([f"weather={a.weather}"] if a.weather else [])
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"preview run failed ({r.returncode}): {r.stderr[-2000:]}")

    # Contact sheet: frames in a grid, each scaled up, with a gap between
    k, gap, cols = a.scale, 6, min(a.cols, len(frames))
    rows_n = (len(frames) + cols - 1) // cols
    tw, th = 320 * k, 240 * k
    sheet_w, sheet_h = cols * tw + (cols - 1) * gap, rows_n * th + (rows_n - 1) * gap
    sheet = [bytearray(b"\x20\x20\x20" * sheet_w) for _ in range(sheet_h)]
    for i, f in enumerate(frames):
        rows = rgb565_rows((OUT / f"{a.scene}_{f}.raw").read_bytes(), 320, 240)
        ox, oy = (i % cols) * (tw + gap), (i // cols) * (th + gap)
        for y, row in enumerate(rows):
            big = b"".join(row[x:x + 3] * k for x in range(0, len(row), 3))
            for dy in range(k):
                sheet[oy + y * k + dy][ox * 3:ox * 3 + len(big)] = big
    write_png(a.out, sheet_w, sheet_h, [bytes(r) for r in sheet])
    print(f"saved {a.out}  ({a.scene}, season {a.season}, {a.time}, frames {a.frames})")


if __name__ == "__main__":
    main()
