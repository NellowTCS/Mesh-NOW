#!/usr/bin/env python3
"""
Universal Mesh-NOW Flash Tool
Python version - Platform agnostic with TUI
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path

try:
    from rich.console import Console
    from rich.prompt import Prompt, IntPrompt
    from rich.panel import Panel
    from rich.text import Text
    from rich.columns import Columns
    from rich.table import Table
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False

def setup_console():
    """Setup console for output"""
    if RICH_AVAILABLE:
        return Console()
    else:
        return None

def run_command(cmd, console=None, ci_mode=False, cwd=None):
    """Run a command and return success"""
    try:
        if ci_mode:
            result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)
        else:
            result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)

        if result.returncode != 0:
            if console and not ci_mode:
                console.print(f"[red]Command failed: {cmd}[/red]")
                console.print(f"[dim]{result.stderr}[/dim]")
            return False, result.stderr
        return True, result.stdout
    except Exception as e:
        if console and not ci_mode:
            console.print(f"[red]Error running command: {e}[/red]")
        return False, str(e)

def find_firmware_dirs(search_path=None):
    """Find available firmware directories"""
    if search_path is None:
        # Default search paths
        search_paths = [
            Path("../release/firmware"),
            Path("./firmware"),
            Path("./release/firmware")
        ]
    else:
        search_paths = [Path(search_path)]

    firmware_dirs = {}

    for search_path in search_paths:
        if search_path.exists() and search_path.is_dir():
            for item in search_path.iterdir():
                if item.is_dir() and (item / "mesh-now.bin").exists():
                    target_name = item.name
                    firmware_dirs[target_name] = item

    return firmware_dirs

def show_target_table(targets, console):
    """Show available targets in a table"""
    if not console:
        return

    table = Table(title="Available ESP32 Targets")
    table.add_column("ID", style="cyan", no_wrap=True)
    table.add_column("Target", style="magenta")
    table.add_column("Description", style="white")

    target_info = {
        "esp32": "ESP32 DevKit C (Dual-core Xtensa, 520KB RAM)",
        "esp32s2": "ESP32-S2 Saola (Single-core Xtensa, 320KB RAM)",
        "esp32s3": "ESP32-S3 DevKit C (Dual-core Xtensa, 512KB RAM)",
        "esp32c3": "ESP32-C3 DevKit C (RISC-V, 400KB RAM)",
        "esp32c6": "ESP32-C6 DevKit C (RISC-V + 802.15.4, 512KB RAM)"
    }

    for i, (target, path) in enumerate(targets.items(), 1):
        desc = target_info.get(target, "ESP32 variant")
        table.add_row(str(i), target, desc)

    console.print(table)
    console.print()

def select_target_interactive(targets, console):
    """Interactive target selection"""
    if not console:
        return None

    while True:
        try:
            choice = IntPrompt.ask("Select target", choices=[str(i) for i in range(1, len(targets) + 1)])
            target_names = list(targets.keys())
            if 1 <= choice <= len(target_names):
                selected_target = target_names[choice - 1]
                return selected_target, targets[selected_target]
        except KeyboardInterrupt:
            console.print("\n[yellow]Cancelled[/yellow]")
            return None

        console.print("[red]Invalid selection. Please try again.[/red]")

def select_port(console, ci_mode=False):
    """Select serial port"""
    if ci_mode:
        return "/dev/ttyUSB0" if os.name != 'nt' else "COM1"

    if console:
        default_port = "/dev/ttyUSB0" if os.name != 'nt' else "COM1"
        port = Prompt.ask("Enter COM port", default=default_port)
        return port
    else:
        return "/dev/ttyUSB0" if os.name != 'nt' else "COM1"

def flash_target(target_name, target_path, port, console, ci_mode=False):
    """Flash firmware for selected target"""
    if console and not ci_mode:
        console.print(f"[bold blue]Flashing {target_name} firmware...[/bold blue]")
        console.print()

    # Change to target directory
    original_cwd = Path.cwd()
    try:
        os.chdir(target_path)

        # Check if flash.py exists in target directory
        flash_script = target_path / "flash.py"
        if flash_script.exists():
            if console and not ci_mode:
                console.print("[green]Using target-specific flash script[/green]")
            cmd = f"{sys.executable} flash.py {target_name} {port}"
            if ci_mode:
                cmd += " --ci"
        else:
            if console and not ci_mode:
                console.print("[yellow]Using manual flash command[/yellow]")
            # Manual flash command
            cmd = f"""esptool.py --chip {target_name} --port {port} --baud 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 40m --flash_size detect 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 mesh-now.bin"""

        success, output = run_command(cmd, console, ci_mode)

        if success:
            if console and not ci_mode:
                console.print("[green]✓ Flash complete![/green]")
                console.print()
                console.print("[bold]Next steps:[/bold]")
                console.print("  1. Reset your ESP32 (press RESET button)")
                console.print("  2. Connect to 'MESH-NOW' WiFi network")
                console.print("  3. Password: 'password'")
                console.print("  4. Open http://192.168.4.1 in your browser")
            elif ci_mode:
                print(f"Successfully flashed {target_name}")
            return True
        else:
            if console and not ci_mode:
                console.print(f"[red]Failed to flash {target_name}[/red]")
            return False

    finally:
        os.chdir(original_cwd)

def main():
    parser = argparse.ArgumentParser(description="Universal Mesh-NOW flash tool")
    parser.add_argument("firmware_dir", nargs="?", help="Path to firmware directory")
    parser.add_argument("--ci", action="store_true", help="CI mode - minimal output")
    args = parser.parse_args()

    console = setup_console() if not args.ci else None

    if console and not args.ci:
        # Show header
        title = Text("Mesh-NOW Universal Flash Tool", style="bold blue")
        subtitle = Text("Cross-platform ESP32 firmware flasher", style="dim")
        panel = Panel(f"{title}\n{subtitle}", border_style="blue")
        console.print(panel)
        console.print()

    # Find firmware directories
    firmware_dirs = find_firmware_dirs(args.firmware_dir)

    if not firmware_dirs:
        if console and not args.ci:
            console.print("[red]No firmware directories found![/red]")
            console.print("Searched in:")
            console.print("  - ../release/firmware")
            console.print("  - ./firmware")
            console.print("  - ./release/firmware")
            console.print()
            console.print("Use: python flash_mesh_now.py /path/to/firmware")
        sys.exit(1)

    if console and not args.ci:
        console.print(f"[green]Found {len(firmware_dirs)} firmware target(s)[/green]")
        show_target_table(firmware_dirs, console)

    # Select target
    if len(firmware_dirs) == 1:
        # Only one target, use it
        target_name, target_path = list(firmware_dirs.items())[0]
        if console and not args.ci:
            console.print(f"[dim]Using only available target: {target_name}[/dim]")
    else:
        # Multiple targets, let user choose
        if args.ci:
            # In CI mode, can't prompt, so fail
            if console:
                console.print("[red]Multiple targets found in CI mode. Please specify target directory.[/red]")
            sys.exit(1)
        else:
            result = select_target_interactive(firmware_dirs, console)
            if result is None:
                return
            target_name, target_path = result

    # Select port
    port = select_port(console, args.ci)

    if console and not args.ci:
        console.print(f"[dim]Selected port: {port}[/dim]")
        console.print()

    # Flash the target
    success = flash_target(target_name, target_path, port, console, args.ci)

    if not success:
        sys.exit(1)

if __name__ == "__main__":
    main()