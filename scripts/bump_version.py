#!/usr/bin/env python3
"""Keep every package manifest on the same version.

Usage:
    python3 scripts/bump_version.py 1.0.0   bump all manifests to 1.0.0
    python3 scripts/bump_version.py --check fail if the manifests disagree
    python3 scripts/bump_version.py --check --expected 1.0.0
        fail unless every manifest holds 1.0.0 (CI guard before tagging)
"""

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+$")

# (path relative to the repo root, regex capturing the current version)
MANIFESTS = (
    (Path("library.json"), re.compile(r'^(\s*"version": ")([^"]+)(")', re.M)),
    (Path("library.properties"), re.compile(r"^(version=)(.*)($)", re.M)),
    (Path("Build/idf_component.yml"), re.compile(r'^(version: ")([^"]+)(")$', re.M)),
)


def read_versions():
    versions = {}
    missing = []
    for path, pattern in MANIFESTS:
        manifest = REPO_ROOT / path
        if not manifest.is_file():
            missing.append(str(path))
            continue
        match = pattern.search(manifest.read_text(encoding="utf-8"))
        versions[str(path)] = match.group(2) if match else None
    return versions, missing


def print_versions(versions):
    for path, version in versions.items():
        print(f"  {path}: {version}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", nargs="?", help="new version, e.g. 1.0.0")
    parser.add_argument(
        "--check", action="store_true", help="verify manifests without writing"
    )
    parser.add_argument(
        "--expected",
        metavar="VERSION",
        help="version manifests must hold when --check is set",
    )
    args = parser.parse_args()

    if args.version and args.check:
        parser.error("version and --check are mutually exclusive")

    versions, missing = read_versions()
    if missing:
        print("error: missing manifests:")
        for path in missing:
            print(f"  {path}")
        return 1

    if args.check:
        target = args.expected
        if target and not SEMVER_RE.match(target):
            print(f"error: --expected must be MAJOR.MINOR.PATCH, got {target!r}")
            return 2
        if target is None:
            target = next(iter(versions.values()))
        mismatches = [(p, v) for p, v in versions.items() if v != target]
        print_versions(versions)
        if mismatches:
            print(f"error: manifests not at {target}:")
            for path, version in mismatches:
                print(f"  {path}: {version}")
            return 1
        print(f"ok: all manifests at {target}")
        return 0

    if not args.version:
        parser.error("version is required unless --check is used")
    if not SEMVER_RE.match(args.version):
        print(f"error: version must be MAJOR.MINOR.PATCH, got {args.version!r}")
        return 2

    for path, pattern in MANIFESTS:
        manifest = REPO_ROOT / path
        text = pattern.sub(
            lambda m: m.group(1) + args.version + m.group(3),
            manifest.read_text(encoding="utf-8"),
        )
        manifest.write_text(text, encoding="utf-8")

    print(f"bumped version to {args.version}:")
    print_versions(read_versions()[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
