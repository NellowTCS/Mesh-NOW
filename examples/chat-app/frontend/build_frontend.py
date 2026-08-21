#!/usr/bin/env python3
"""
Mesh-NOW Frontend Builder
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

def check_nodejs(console, ci_mode=False):
    """Check if Node.js is available"""
    success, output = run_command("node --version", console, ci_mode)
    if not success:
        if console and not ci_mode:
            console.print("[red]Error: Node.js not found[/red]")
            console.print("Please install Node.js from https://nodejs.org")
        return False, ""

    version = output.strip()
    if console and not ci_mode:
        console.print(f"[green]✓ Node.js found: {version}[/green]")
    return True, version

def check_npm(console, ci_mode=False):
    """Check if npm is available"""
    success, output = run_command("npm --version", console, ci_mode)
    if not success:
        if console and not ci_mode:
            console.print("[red]Error: npm not found[/red]")
            console.print("Please install npm (usually comes with Node.js)")
        return False, ""

    version = output.strip()
    if console and not ci_mode:
        console.print(f"[green]✓ npm found: {version}[/green]")
    return True, version

def install_dependencies(console, script_dir, ci_mode=False):
    """Install npm dependencies if needed"""
    node_modules = script_dir / "node_modules"
    if not node_modules.exists():
        if console and not ci_mode:
            console.print("[yellow]Installing dependencies...[/yellow]")

        success, _ = run_command("npm install", console, ci_mode, cwd=script_dir)
        if not success:
            if console and not ci_mode:
                console.print("[red]Failed to install dependencies[/red]")
            return False

        if console and not ci_mode:
            console.print("[green]✓ Dependencies installed[/green]")
    else:
        if console and not ci_mode:
            console.print("[green]✓ Dependencies already installed[/green]")

    return True

def build_frontend(console, script_dir, ci_mode=False):
    """Build the frontend"""
    if console and not ci_mode:
        console.print("[bold blue]Building frontend...[/bold blue]")
        console.print()

    success, _ = run_command("npm run build", console, ci_mode, cwd=script_dir)
    if not success:
        if console and not ci_mode:
            console.print("[red]Frontend build failed[/red]")
        return False

    # Check if dist directory was created
    dist_dir = script_dir / "dist"
    if not dist_dir.exists():
        if console and not ci_mode:
            console.print("[red]Build completed but dist directory not found[/red]")
        return False

    # List built files
    if console and not ci_mode:
        console.print("[green]✓ Frontend built successfully![/green]")
        console.print("[dim]Built files:[/dim]")

        for file_path in dist_dir.iterdir():
            if file_path.is_file():
                size = file_path.stat().st_size
                size_str = f"{size}B" if size < 1024 else f"{size//1024}KB"
                console.print(f"  {file_path.name}: {size_str}")

    return True

def main():
    parser = argparse.ArgumentParser(description="Build Mesh-NOW frontend")
    parser.add_argument("--ci", action="store_true", help="CI mode - minimal output")
    args = parser.parse_args()

    console = setup_console() if not args.ci else None

    if console and not args.ci:
        # Show header
        title = Text("Mesh-NOW Frontend Builder", style="bold blue")
        panel = Panel(title, border_style="blue")
        console.print(panel)
        console.print()

    # Get script directory (frontend directory)
    script_dir = Path(__file__).parent

    # Check Node.js and npm
    if not check_nodejs(console, args.ci)[0]:
        sys.exit(1)

    if not check_npm(console, args.ci)[0]:
        sys.exit(1)

    if console and not args.ci:
        console.print()

    # Install dependencies
    if not install_dependencies(console, script_dir, args.ci):
        sys.exit(1)

    if console and not args.ci:
        console.print()

    # Build frontend
    if not build_frontend(console, script_dir, args.ci):
        sys.exit(1)

    if console and not args.ci:
        console.print()
        console.print("[green]Frontend build completed![/green]")

if __name__ == "__main__":
    main()