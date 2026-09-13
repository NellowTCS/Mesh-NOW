#!/usr/bin/env python3
"""
Mesh-NOW Formatter

Usage:
    python3 scripts/format.py            # format everything
    python3 scripts/format.py --check    # verify everything is formatted
    python3 scripts/format.py --c        # C/C++ only
    python3 scripts/format.py --fe       # frontend only
    python3 scripts/format.py --py       # python only
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

C_DIRS = (ROOT / "components", ROOT / "examples" / "chat-app" / "main")
GENERATED_HEADERS = {"bundle_js.h", "index_html.h", "styles_css.h"}
FRONTEND_DIR = ROOT / "Demo"
FMT_EXCLUDE = r"/(mpack|node_modules|build|builds|\.git)/"


def c_files():
    for base in C_DIRS:
        for path in base.rglob("*"):
            if path.suffix not in (".c", ".h"):
                continue
            if "mpack" in path.parts:
                continue
            if path.name in GENERATED_HEADERS:
                continue
            yield path


def run_c(clang_format, check):
    files = sorted(str(p) for p in c_files())
    if not files:
        return 0
    cmd = [clang_format, "-style=file"]
    cmd += ["--dry-run", "--Werror"] if check else ["-i"]
    cmd += files
    return subprocess.call(cmd)


def run_fe(check):
    script = "format:check" if check else "format"
    return subprocess.call(["npm", "run", script], cwd=str(FRONTEND_DIR))


def run_py(black, check):
    cmd = [black]
    cmd += ["--check"] if check else []
    cmd += ["--skip-string-normalization", "--extend-exclude", FMT_EXCLUDE]
    cmd += ["components", "examples", "scripts"]
    return subprocess.call(cmd)


def main():
    parser = argparse.ArgumentParser(description="Format Mesh-NOW sources")
    parser.add_argument(
        "--check",
        action="store_true",
        help="report files that would change instead of writing",
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--c", action="store_true", help="C/C++ only")
    group.add_argument("--fe", action="store_true", help="frontend only")
    group.add_argument("--py", action="store_true", help="python only")
    args = parser.parse_args()

    any_lang = not (args.c or args.fe or args.py)

    failures = []

    if any_lang or args.c:
        clang_format = shutil.which("clang-format")
        if clang_format is None:
            print("error: clang-format not found (brew install clang-format)")
            failures.append("c")
        else:
            print("Formatting C/C++ ...")
            if run_c(clang_format, args.check) != 0:
                failures.append("c")

    if any_lang or args.fe:
        if shutil.which("npm") is None:
            print("error: npm not found")
            failures.append("fe")
        else:
            print("Formatting frontend ...")
            if run_fe(args.check) != 0:
                failures.append("fe")

    if any_lang or args.py:
        black = shutil.which("black")
        if black is None:
            print("error: black not found (pipx install black)")
            failures.append("py")
        else:
            print("Formatting python ...")
            if run_py(black, args.check) != 0:
                failures.append("py")

    if failures:
        print("format failed: " + ", ".join(failures))
        sys.exit(1)


if __name__ == "__main__":
    main()
