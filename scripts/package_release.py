#!/usr/bin/env python3

import argparse
import hashlib
import os
import sys
import tarfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILDS_DIR = REPO_ROOT / "Firmware" / "builds"
RELEASE_DIR = REPO_ROOT / "release"

FLASH_TOOLS = ["install_esptool.py", "flash_mesh_now.py"]
FIRMWARE_ARTIFACTS = ["mesh-now.bin", "bootloader.bin", "partition-table.bin"]
DOCS = {"README.md": "documentation/README.md",
        "Docs/docs/getting-started/quickstart.md": "documentation/QUICKSTART.md"}


def assemble_tree(tag: str) -> None:
    """Copy flash tools, docs, and per-target firmware into release/."""
    RELEASE_DIR.mkdir(parents=True, exist_ok=True)
    firmware_dir = RELEASE_DIR / "firmware"
    flash_tools_dir = RELEASE_DIR / "flash-tools"
    docs_dir = RELEASE_DIR / "documentation"
    for d in [firmware_dir, flash_tools_dir, docs_dir]:
        d.mkdir(parents=True, exist_ok=True)

    for tool in FLASH_TOOLS:
        tool_path = REPO_ROOT / "scripts" / tool
        if not tool_path.exists():
            print(f"Missing flash tool: {tool}", file=sys.stderr)
            sys.exit(1)
        shutil_copy(tool_path, flash_tools_dir / tool)

    target_dirs = sorted(p for p in BUILDS_DIR.iterdir() if p.is_dir())
    if not target_dirs:
        print("No builds found in Firmware/builds/", file=sys.stderr)
        print("Run `just build-all` first.", file=sys.stderr)
        sys.exit(1)

    for target_dir in target_dirs:
        target_name = target_dir.name
        print(f"Packaging {target_name}...")
        target_out = firmware_dir / target_name
        target_out.mkdir(parents=True, exist_ok=True)
        for artifact in FIRMWARE_ARTIFACTS:
            artifact_path = target_dir / artifact
            if not artifact_path.exists():
                print(f"Missing artifact: {artifact_path}", file=sys.stderr)
                sys.exit(1)
            shutil_copy(artifact_path, target_out / artifact)
        shutil_copy(REPO_ROOT / "scripts" / "flash.py",
                    target_out / "flash.py", required=True)
        tarball = firmware_dir / f"mesh-now-{target_name}-{tag}.tar.gz"
        with tarfile.open(tarball, "w:gz") as tar:
            tar.add(target_out, arcname=target_name)
        # Keep only the tarball; the raw files ship inside it.
        for artifact in FIRMWARE_ARTIFACTS + ["flash.py"]:
            (target_out / artifact).unlink()
        target_out.rmdir()

    for src, dst in DOCS.items():
        src_path = REPO_ROOT / src
        if not src_path.exists():
            print(f"Missing doc: {src}", file=sys.stderr)
            sys.exit(1)
        (RELEASE_DIR / dst).parent.mkdir(parents=True, exist_ok=True)
        shutil_copy(src_path, RELEASE_DIR / dst)


def shutil_copy(src: Path, dest: Path, required: bool = True) -> None:
    """Copy src to dest (a file path); abort when required and missing."""
    if not src.exists():
        if required:
            print(f"Missing file: {src}", file=sys.stderr)
            sys.exit(1)
        return
    dest.write_bytes(src.read_bytes())


def write_checksums() -> None:
    """SHA256 of every tarball and binary in release/, sorted by path."""
    sha_lines = []
    for path in sorted(RELEASE_DIR.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix not in (".gz", ".bin"):
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        sha_lines.append(f"{digest}  {path.relative_to(RELEASE_DIR)}")
    (RELEASE_DIR / "SHA256SUMS").write_text("\n".join(sha_lines) + "\n")
    print("\n".join(sha_lines))


def main():
    parser = argparse.ArgumentParser(description="Assemble Mesh-NOW release tree")
    parser.add_argument("tag", help="Release tag used in tarball names")
    args = parser.parse_args()

    os.chdir(REPO_ROOT)
    assemble_tree(args.tag)
    write_checksums()
    print("\nRelease ready in release/")
    print(f"  tar -xzf release/mesh-now-<target>-{args.tag}.tar.gz")


if __name__ == "__main__":
    main()