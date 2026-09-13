#!/usr/bin/env python3
"""Host unit-test runner for Mesh-NOW.
Usage:
    python3 scripts/test_unit.py
    python3 scripts/test_unit.py --clean
    python3 scripts/test_unit.py --warn-as-error
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TESTS_DIR = REPO_ROOT / "tests" / "unit"
BUILD_DIR = TESTS_DIR / "build"


def run(argv, cwd=None):
    print("+ " + " ".join(argv))
    return subprocess.run(argv, cwd=cwd)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--clean", action="store_true", help="wipe the build directory first"
    )
    parser.add_argument(
        "--warn-as-error",
        action="store_true",
        help="treat compiler warnings as failures (sets -Werror)",
    )
    args = parser.parse_args()

    if args.clean and BUILD_DIR.is_dir():
        shutil.rmtree(BUILD_DIR)

    configure = ["cmake", "-S", str(TESTS_DIR), "-B", str(BUILD_DIR)]
    if args.warn_as_error:
        configure.append("-DUNIT_TESTS_WERROR=ON")

    for argv in (
        configure,
        ["cmake", "--build", str(BUILD_DIR), "--parallel"],
        ["ctest", "--test-dir", str(BUILD_DIR), "--output-on-failure"],
    ):
        result = run(argv)
        if result.returncode != 0:
            return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
