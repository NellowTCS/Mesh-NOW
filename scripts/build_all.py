#!/usr/bin/env python3
"""
Mesh-NOW Multi-Target Build Script
"""

import os
import sys
import subprocess
import argparse
import shutil
from pathlib import Path

try:
    from rich.console import Console
    from rich.progress import Progress, SpinnerColumn, TextColumn, BarColumn
    from rich.panel import Panel
    from rich.table import Table
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False

def check_idf_setup(console):
    """Check if ESP-IDF environment is set up"""
    idf_path = os.environ.get('IDF_PATH')
    if not idf_path:
        console.print("[red]Error: ESP-IDF environment not set up[/red]")
        console.print("Please run: source $ESP_IDF_PATH/export.sh")
        sys.exit(1)
    return idf_path

def run_command(cmd, cwd=None, console=None, capture_output=False):
    """Run a command and return success and output"""
    try:
        if capture_output:
            result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)
        else:
            result = subprocess.run(cmd, shell=True, cwd=cwd)
        return result.returncode == 0, result.stdout if capture_output else None
    except Exception as e:
        if console:
            console.print(f"[red]Error running command: {e}[/red]")
        return False, None

def get_targets():
    """Get available targets"""
    return ["esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"]

def build_target(target, console, script_dir, progress=None):
    """Build for a specific target"""
    build_dir = script_dir.parent / "build"

    # Clean previous build
    if build_dir.exists():
        shutil.rmtree(build_dir)

    task = progress.add_task(f"Building {target}...", total=4) if progress else None

    # Set target
    if progress:
        progress.update(task, description=f"Setting target {target}...")
    else:
        console.print(f"Setting target to {target}...")
    success, _ = run_command(f"idf.py set-target {target}", console=console)
    if not success:
        if progress:
            progress.update(task, description=f"Failed to set target {target}")
        return False
    if progress:
        progress.advance(task)

    # Copy target-specific config
    config_file = script_dir.parent / "configs" / f"sdkconfig.{target}"
    if config_file.exists():
        if progress:
            progress.update(task, description=f"Applying config for {target}...")
        else:
            console.print(f"Using configuration: {config_file}")
        success, _ = run_command(f"cp {config_file} sdkconfig.defaults", console=console)
        if not success:
            return False
    if progress:
        progress.advance(task)

    # Build
    if progress:
        progress.update(task, description=f"Building {target}...")
    else:
        console.print(f"Building for {target}...")
    success, _ = run_command("idf.py build", console=console)
    if not success:
        if progress:
            progress.update(task, description=f"Build failed for {target}")
        return False
    if progress:
        progress.advance(task)

    # Copy artifacts
    builds_dir = script_dir.parent / "builds" / target
    builds_dir.mkdir(parents=True, exist_ok=True)

    artifacts = [
        ("mesh-now.bin", "build/mesh-now.bin"),
        ("bootloader.bin", "build/bootloader/bootloader.bin"),
        ("partition-table.bin", "build/partition_table/partition-table.bin")
    ]

    for name, src in artifacts:
        src_path = script_dir.parent / src
        if src_path.exists():
            shutil.copy2(src_path, builds_dir / name)

    # Get binary size
    bin_file = builds_dir / "mesh-now.bin"
    if bin_file.exists():
        size = bin_file.stat().st_size
        size_kb = size // 1024
        if progress:
            progress.update(task, description=f"{target}: {size_kb}KB")
        else:
            console.print(f"   Build successful! Binary size: {size_kb}KB")
            console.print(f"   Artifacts saved to: {builds_dir}")

    if progress:
        progress.advance(task)

    return True

def main():
    parser = argparse.ArgumentParser(description="Mesh-NOW Multi-Target Build Script")
    parser.add_argument('--ci', action='store_true', help='Disable colors and TUI for CI')
    parser.add_argument('--targets', nargs='*', help='Build specific targets (default: all)')
    args = parser.parse_args()

    # Setup console
    if RICH_AVAILABLE and not args.ci:
        console = Console()
    else:
        class PlainConsole:
            def print(self, *args, **kwargs):
                if args:
                    import re
                    text = re.sub(r'\[.*?\]', '', str(args[0]))
                    print(text)
                else:
                    print()
        console = PlainConsole()

    # Check IDF setup
    check_idf_setup(console)

    script_dir = Path(__file__).parent
    os.chdir(script_dir.parent)  # Change to project root

    targets = args.targets if args.targets else get_targets()

    console.print(Panel.fit("[bold blue]Mesh-NOW Multi-Target Build Script[/bold blue]"))
    console.print(f"Targets: {', '.join(targets)}")
    console.print()

    failed_builds = []
    successful_builds = []

    if RICH_AVAILABLE and not args.ci:
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            BarColumn(),
            console=console
        ) as progress:
            for target in targets:
                if build_target(target, console, script_dir, progress):
                    successful_builds.append(target)
                else:
                    failed_builds.append(target)
    else:
        for target in targets:
            console.print(f"Building for {target}...")
            if build_target(target, console, script_dir):
                successful_builds.append(target)
            else:
                failed_builds.append(target)
                console.print(f"[red]{target}: FAILED[/red]")
            console.print()

    # Summary
    console.print("=" * 40)
    console.print("Build Summary:")
    console.print("=" * 40)

    table = Table() if RICH_AVAILABLE and not args.ci else None
    if table:
        table.add_column("Status", style="bold")
        table.add_column("Targets")
        table.add_row("Successful", f"{len(successful_builds)}: {', '.join(successful_builds)}")
        if failed_builds:
            table.add_row("Failed", f"{len(failed_builds)}: {', '.join(failed_builds)}", style="red")
        console.print(table)
    else:
        console.print(f"Successful builds ({len(successful_builds)}): {', '.join(successful_builds)}")
        if failed_builds:
            console.print(f"Failed builds ({len(failed_builds)}): {', '.join(failed_builds)}")

    console.print()
    console.print("Build artifacts location:")
    console.print("   builds/")
    for target in successful_builds:
        console.print(f"   ├── {target}/")
        console.print("   │   ├── mesh-now.bin")
        console.print("   │   ├── bootloader.bin")
        console.print("   │   └── partition-table.bin")

    console.print()
    console.print("Flash commands:")
    for target in successful_builds:
        console.print(f"   {target}: idf.py set-target {target} && idf.py flash")

    if not failed_builds:
        console.print()
        console.print("[green]All builds completed successfully![/green]")
        sys.exit(0)
    else:
        console.print()
        console.print("[red]Some builds failed. Check the output above for details.[/red]")
        sys.exit(1)

if __name__ == "__main__":
    main()