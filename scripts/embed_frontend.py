#!/usr/bin/env python3
"""
Mesh-NOW Frontend Embedder
Convert built frontend files to C headers for ESP32 embedding
Python version - Platform agnostic with TUI
"""

import os
import sys
import argparse
from pathlib import Path

try:
    from rich.console import Console
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

def file_to_header(input_file, output_file, var_name, console=None, ci_mode=False):
    """Convert a file to a C header"""
    if console and not ci_mode:
        console.print(f"[dim]Converting {input_file.name} to {output_file.name}...[/dim]")

    try:
        # Read the file as binary
        with open(input_file, 'rb') as f:
            data = f.read()

        file_size = len(data)

        # Convert to C array
        with open(output_file, 'w') as f:
            f.write(f"#ifndef {var_name}_H\n")
            f.write(f"#define {var_name}_H\n")
            f.write("\n")
            f.write(f"const char {var_name}[] = {{\n")

            # Convert bytes to hex
            hex_bytes = [f"0x{b:02x}" for b in data]

            # Group into lines of 12 bytes for readability
            for i in range(0, len(hex_bytes), 12):
                line_bytes = hex_bytes[i:i+12]
                f.write("    " + ", ".join(line_bytes) + ",\n")

            f.write("};\n")
            f.write("\n")
            f.write(f"const size_t {var_name}_size = {file_size};\n")
            f.write("\n")
            f.write(f"#endif // {var_name}_H\n")

        return True

    except Exception as e:
        if console and not ci_mode:
            console.print(f"[red]Error converting {input_file.name}: {e}[/red]")
        return False

def main():
    parser = argparse.ArgumentParser(description="Convert frontend files to C headers")
    parser.add_argument("--ci", action="store_true", help="CI mode - minimal output")
    args = parser.parse_args()

    console = setup_console() if not args.ci else None

    if console and not args.ci:
        # Show header
        title = Text("Mesh-NOW Frontend Embedder", style="bold blue")
        panel = Panel(title, border_style="blue")
        console.print(panel)
        console.print()

    # Get directories
    script_dir = Path(__file__).parent
    project_dir = script_dir.parent
    frontend_dir = project_dir / "frontend"
    dist_dir = frontend_dir / "dist"
    output_dir = project_dir / "main"

    # Check if dist directory exists
    if not dist_dir.exists():
        if console and not args.ci:
            console.print("[red]Error: Frontend dist directory not found[/red]")
            console.print("Run frontend build first: python frontend/build_frontend.py")
        else:
            print("Error: Frontend dist directory not found")
        sys.exit(1)

    if console and not args.ci:
        console.print(f"[green]✓ Found frontend dist: {dist_dir}[/green]")
        console.print()

    # Ensure output directory exists
    output_dir.mkdir(exist_ok=True)

    # Files to convert
    conversions = [
        ("index.html", "index_html.h", "INDEX_HTML"),
        ("bundle.js", "bundle_js.h", "BUNDLE_JS"),
        ("styles.css", "styles_css.h", "STYLES_CSS"),
    ]

    converted_files = []

    for input_name, output_name, var_name in conversions:
        input_file = dist_dir / input_name
        output_file = output_dir / output_name

        if input_file.exists():
            if file_to_header(input_file, output_file, var_name, console, args.ci):
                converted_files.append(output_name)
                if console and not args.ci:
                    # Show file size
                    size = output_file.stat().st_size
                    size_str = f"{size}B" if size < 1024 else f"{size//1024}KB"
                    console.print(f"[green]✓ {output_name}: {size_str}[/green]")
        else:
            if console and not args.ci:
                console.print(f"[yellow]⚠ {input_name} not found, skipping[/yellow]")

    if console and not args.ci:
        console.print()
        if converted_files:
            console.print("[green]✓ Frontend embedding complete![/green]")
            console.print("[dim]Generated headers:[/dim]")
            for header in converted_files:
                console.print(f"  main/{header}")
        else:
            console.print("[red]No files were converted[/red]")
            sys.exit(1)

if __name__ == "__main__":
    main()