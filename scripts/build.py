#!/usr/bin/env python3
"""
Mesh-NOW Target Selector and Builder
Python version - Platform agnostic with TUI
"""

import os
import sys
import subprocess
import argparse
import shutil
from pathlib import Path

try:
    from rich.console import Console
    from rich.prompt import Prompt, IntPrompt
    from rich.panel import Panel
    from rich.text import Text
    from rich.columns import Columns
    from rich.progress import Progress, SpinnerColumn, TextColumn, BarColumn
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

def run_command(cmd, cwd=None, console=None):
    """Run a command and return success"""
    try:
        result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)
        if result.returncode != 0:
            if console:
                console.print(f"[red]Command failed: {cmd}[/red]")
                console.print(f"[dim]{result.stderr}[/dim]")
            return False
        return True
    except Exception as e:
        if console:
            console.print(f"[red]Error running command: {e}[/red]")
        return False

def get_targets():
    """Get available targets with descriptions"""
    return {
        1: ("esp32", "ESP32 DevKit C (Dual-core Xtensa, 520KB RAM)"),
        2: ("esp32s2", "ESP32-S2 Saola (Single-core Xtensa, 320KB RAM)"),
        3: ("esp32s3", "ESP32-S3 DevKitM (Dual-core Xtensa, 512KB RAM + PSRAM)"),
        4: ("esp32c3", "ESP32-C3 DevKitM (Single-core RISC-V, 400KB RAM)"),
        5: ("esp32c6", "ESP32-C6 DevKitC (Single-core RISC-V, 512KB RAM + 802.15.4)"),
    }

def show_menu(console, targets):
    """Show target selection menu"""
    console.print(Panel.fit("[bold blue]Mesh-NOW Target Selector[/bold blue]"))
    console.print()

    for num, (target, desc) in targets.items():
        console.print(f"[cyan]{num})[/cyan] [yellow]{target}[/yellow] - {desc}")

    console.print("[cyan]6)[/cyan] Build all targets")
    console.print("[cyan]0)[/cyan] Exit")
    console.print()

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
    success = run_command(f"idf.py set-target {target}", console=console)
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
        success = run_command(f"cp {config_file} sdkconfig.defaults", console=console)
        if not success:
            return False
    if progress:
        progress.advance(task)

    # Build
    if progress:
        progress.update(task, description=f"Building {target}...")
    else:
        console.print(f"Building for {target}...")
    success = run_command("idf.py build", console=console)
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

def build_all_targets(console, script_dir, ci_mode=False):
    """Build all targets"""
    targets = ["esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"]

    console.print(Panel.fit("[bold blue]Building All ESP32 Targets[/bold blue]"))
    console.print(f"Targets: {', '.join(targets)}")
    console.print()

    failed_builds = []
    successful_builds = []

    if RICH_AVAILABLE and not ci_mode:
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

    table = Table() if RICH_AVAILABLE and not ci_mode else None
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
        return True
    else:
        console.print()
        console.print("[red]Some builds failed. Check the output above for details.[/red]")
        return False

def build_frontend(console, project_dir, ci_mode=False):
    """Build the frontend"""
    if console and not ci_mode:
        console.print("[bold blue]Building frontend...[/bold blue]")

    frontend_dir = project_dir / "frontend"
    build_script = frontend_dir / "build_frontend.py"

    if not build_script.exists():
        if console and not ci_mode:
            console.print("[red]Frontend build script not found[/red]")
        return False

    success = run_command(f"python {build_script} {'--ci' if ci_mode else ''}", cwd=frontend_dir, console=console)
    if not success:
        if console and not ci_mode:
            console.print("[red]Frontend build failed[/red]")
        return False

    if console and not ci_mode:
        console.print("[green]✓ Frontend built successfully[/green]")
    return True

def embed_frontend(console, project_dir, ci_mode=False):
    """Embed frontend files into ESP32 firmware"""
    if console and not ci_mode:
        console.print("[bold blue]Embedding frontend files...[/bold blue]")

    embed_script = project_dir / "scripts" / "embed_frontend.py"

    if not embed_script.exists():
        if console and not ci_mode:
            console.print("[red]Embed script not found[/red]")
        return False

    success = run_command(f"python {embed_script} {'--ci' if ci_mode else ''}", cwd=project_dir, console=console)
    if not success:
        if console and not ci_mode:
            console.print("[red]Frontend embedding failed[/red]")
        return False

    if console and not ci_mode:
        console.print("[green]✓ Frontend embedded successfully[/green]")
    return True

def main():
    parser = argparse.ArgumentParser(description="Mesh-NOW Target Selector and Builder")
    parser.add_argument('--ci', action='store_true', help='Disable colors and TUI for CI')
    parser.add_argument('--target', type=str, help='Build specific target (non-interactive)')
    parser.add_argument('--with-frontend', action='store_true', help='Build and embed frontend before ESP32 build')
    args = parser.parse_args()

    # Setup console
    if RICH_AVAILABLE and not args.ci:
        console = Console()
    else:
        # Fallback to plain console
        class PlainConsole:
            def print(self, *args, **kwargs):
                if args:
                    # Strip rich markup for plain output
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

    # Handle frontend building if requested
    if args.with_frontend:
        if console and not args.ci:
            console.print(Panel.fit("[bold green]Frontend Integration Enabled[/bold green]"))
            console.print()

        if not build_frontend(console, script_dir.parent, args.ci):
            sys.exit(1)
        if not embed_frontend(console, script_dir.parent, args.ci):
            sys.exit(1)

        if console and not args.ci:
            console.print()

    targets = get_targets()

    if args.target:
        # Non-interactive mode
        if args.target not in [t[0] for t in targets.values()]:
            console.print(f"[red]Invalid target: {args.target}[/red]")
            sys.exit(1)
        build_target(args.target, console, script_dir)
        return

    # Interactive mode
    while True:
        show_menu(console, targets)

        if RICH_AVAILABLE and not args.ci:
            choice = IntPrompt.ask("Select target (0-6)", default=0)
        else:
            try:
                choice = int(input("Select target (0-6): "))
            except ValueError:
                choice = -1

        if choice == 0:
            console.print("Goodbye!")
            break
        elif choice in targets:
            target, desc = targets[choice]
            if build_target(target, console, script_dir):
                break
        elif choice == 6:
            success = build_all_targets(console, script_dir, args.ci)
            if success:
                break
        else:
            console.print("[red]Invalid choice. Please select 0-6.[/red]")
            console.print()

if __name__ == "__main__":
    main()