#!/usr/bin/env python3
"""
ESP32 Tool Installer
Python version - Platform agnostic with TUI
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path

try:
    from rich.console import Console
    from rich.progress import Progress, SpinnerColumn, TextColumn
    from rich.panel import Panel
    from rich.text import Text
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False

def setup_console():
    """Setup console for output"""
    if RICH_AVAILABLE:
        return Console()
    else:
        return None

def run_command(cmd, console=None, ci_mode=False):
    """Run a command and return success"""
    try:
        if ci_mode:
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        else:
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True)

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

def check_python():
    """Check if Python is available"""
    try:
        result = subprocess.run([sys.executable, "--version"], capture_output=True, text=True)
        return result.returncode == 0, result.stdout.strip()
    except:
        return False, ""

def check_pip():
    """Check if pip is available"""
    try:
        result = subprocess.run([sys.executable, "-m", "pip", "--version"], capture_output=True, text=True)
        return result.returncode == 0, result.stdout.strip()
    except:
        return False, ""

def install_esptool(console, ci_mode=False):
    """Install esptool.py"""
    if console and not ci_mode:
        console.print("[bold blue]Installing esptool.py...[/bold blue]")
        console.print()

    # Check Python
    python_ok, python_version = check_python()
    if not python_ok:
        if console and not ci_mode:
            console.print("[red]Error: Python not found[/red]")
        return False

    if console and not ci_mode:
        console.print(f"[green]✓ Python found: {python_version}[/green]")

    # Check pip
    pip_ok, pip_version = check_pip()
    if not pip_ok:
        if console and not ci_mode:
            console.print("[red]Error: pip not found[/red]")
            console.print("Please install pip first")
        return False

    if console and not ci_mode:
        console.print(f"[green]✓ Pip found: {pip_version.split()[0]}[/green]")

    # Install esptool
    if console and not ci_mode:
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            console=console,
        ) as progress:
            task = progress.add_task("Installing esptool...", total=None)
            success, output = run_command(f"{sys.executable} -m pip install esptool", console, ci_mode)
            progress.update(task, completed=True)
    else:
        success, output = run_command(f"{sys.executable} -m pip install esptool", console, ci_mode)

    if not success:
        if console and not ci_mode:
            console.print("[red]Failed to install esptool[/red]")
        return False

    # Verify installation
    success, version_output = run_command("esptool.py version", console, ci_mode)
    if not success:
        if console and not ci_mode:
            console.print("[red]esptool installation verification failed[/red]")
        return False

    if console and not ci_mode:
        console.print("[green]✓ esptool.py installed successfully![/green]")
        console.print(f"[dim]{version_output.strip()}[/dim]")
        console.print()
        console.print("[bold]Usage:[/bold] esptool.py --help")
    elif ci_mode:
        print(f"esptool.py installed: {version_output.strip()}")

    return True

def main():
    parser = argparse.ArgumentParser(description="Install ESP32 flashing tools")
    parser.add_argument("--ci", action="store_true", help="CI mode - minimal output")
    args = parser.parse_args()

    console = setup_console() if not args.ci else None

    if console and not args.ci:
        # Show header
        title = Text("ESP32 Tool Installer", style="bold blue")
        panel = Panel(title, border_style="blue")
        console.print(panel)
        console.print()

    success = install_esptool(console, args.ci)

    if not success:
        if console and not args.ci:
            console.print("[red]Installation failed![/red]")
        sys.exit(1)
    else:
        if console and not args.ci:
            console.print("[green]Installation completed successfully![/green]")

if __name__ == "__main__":
    main()