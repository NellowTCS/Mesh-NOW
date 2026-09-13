# Mesh-NOW Development Commands
#
#   just              List available commands
#   just build esp32  Build firmware for a target
#   just flash esp32  Flash firmware to device

scripts := "scripts"

# Pass-through to build scripts
build *args:
    python3 {{ scripts }}/build.py {{ args }} --ci

build-all *args:
    python3 {{ scripts }}/build_all.py {{ args }} --ci

flash *args:
    cd Firmware && idf.py flash {{ args }}

test-targets *args:
    python3 {{ scripts }}/test_targets.py {{ args }}

# Frontend pipeline
frontend:
    python3 scripts/build_frontend.py --ci

fe-install:
    cd Demo && npm install

fe-watch:
    cd Demo && npm run dev

fe-serve:
    cd Demo && npm run serve

# Formatting
format *flags:
    python3 scripts/format.py {{ flags }}

format-check:
    python3 scripts/format.py --check

# Protocol
protocol:
    ./scripts/compile_protocol.sh

# Docs
docs *args:
    cd Docs && npm run {{ args }}

# Meta
setup:
    git submodule update --init --recursive
    cd Demo && npm install

clean:
    rm -rf Firmware/build Firmware/builds
    rm -rf Demo/dist
    rm -rf Demo/src/generated

test:
    python3 scripts/test_unit.py

all: setup frontend test build
