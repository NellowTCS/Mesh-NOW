#!/usr/bin/env python3
"""
Mesh-NOW Frontend Builder
"""

import argparse
import subprocess
import sys
from pathlib import Path

DEMO_DIR = Path(__file__).resolve().parent.parent / "Demo"


def run_command(cmd, cwd=None):
    """Run a command and return (success, output)."""
    try:
        result = subprocess.run(
            cmd, shell=True, cwd=cwd, capture_output=True, text=True
        )
    except OSError as e:
        print(f"Error running command: {e}", file=sys.stderr)
        return False, str(e)
    if result.returncode != 0:
        print(f"Command failed: {cmd}", file=sys.stderr)
        if result.stderr.strip():
            print(result.stderr, file=sys.stderr, end="")
        return False, result.stderr
    return True, result.stdout


def check_nodejs():
    """Check that Node.js is available."""
    success, output = run_command("node --version")
    if not success:
        print("Error: Node.js not found", file=sys.stderr)
        print("Install Node.js from https://nodejs.org", file=sys.stderr)
        return False
    print(f"Node.js found: {output.strip()}")
    return True


def check_npm():
    """Check that npm is available."""
    success, output = run_command("npm --version")
    if not success:
        print("Error: npm not found", file=sys.stderr)
        print("npm usually comes with Node.js", file=sys.stderr)
        return False
    print(f"npm found: {output.strip()}")
    return True


def install_dependencies():
    """Install npm dependencies if needed."""
    if (DEMO_DIR / "node_modules").exists():
        print("Dependencies already installed")
        return True

    print("Installing dependencies...")
    success, _ = run_command("npm install", cwd=DEMO_DIR)
    if not success:
        print("Failed to install dependencies", file=sys.stderr)
        return False
    print("Dependencies installed")
    return True


def build_frontend():
    """Build the frontend."""
    print("Building frontend...")
    success, _ = run_command("npm run build", cwd=DEMO_DIR)
    if not success:
        print("Frontend build failed", file=sys.stderr)
        return False

    dist_dir = DEMO_DIR / "dist"
    if not dist_dir.exists():
        print("Build completed but dist directory not found", file=sys.stderr)
        return False

    print("Frontend built successfully!")
    print("Built files:")
    for file_path in dist_dir.iterdir():
        if file_path.is_file():
            size = file_path.stat().st_size
            size_str = f"{size}B" if size < 1024 else f"{size // 1024}KB"
            print(f"  {file_path.name}: {size_str}")
    return True


def main():
    parser = argparse.ArgumentParser(description="Build Mesh-NOW frontend")
    parser.parse_args()

    if not check_nodejs():
        sys.exit(1)

    if not check_npm():
        sys.exit(1)

    print()
    if not install_dependencies():
        sys.exit(1)

    print()
    if not build_frontend():
        sys.exit(1)

    print()
    print("Frontend build completed!")


if __name__ == "__main__":
    main()