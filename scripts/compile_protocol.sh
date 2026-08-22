#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
KSY_FILE="$REPO_ROOT/mesh_now.ksy"
OUTPUT_DIR="$REPO_ROOT/examples/chat-app/frontend/src/generated"

if command -v ksc &>/dev/null; then
    COMPILER=ksc
elif command -v kaitai-struct-compiler &>/dev/null; then
    COMPILER=kaitai-struct-compiler
else
    echo "Error: kaitai-struct-compiler not found" >&2
    echo "Install: brew install kaitai-struct-compiler" >&2
    exit 1
fi

if [ ! -f "$KSY_FILE" ]; then
    echo "Error: $KSY_FILE not found" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
$COMPILER -t javascript --outdir "$OUTPUT_DIR" "$KSY_FILE"
echo "Generated JS parser in $OUTPUT_DIR"
