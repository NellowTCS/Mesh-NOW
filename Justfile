# Mesh-NOW Development Commands
#
#   just              List available commands
#   just build esp32  Build firmware for a target
#   just flash esp32  Flash firmware to device

scripts := "examples/chat-app/scripts"

# Pass-through to build scripts
build *args:
    python3 {{scripts}}/build.py {{args}} --ci

build-all *args:
    python3 {{scripts}}/build_all.py {{args}} --ci

flash *args:
    cd examples/chat-app && idf.py flash {{args}}

test-targets *args:
    python3 {{scripts}}/test_targets.py {{args}}

# Frontend pipeline
frontend:
    python3 examples/chat-app/frontend/build_frontend.py --ci

fe-install:
    cd examples/chat-app/frontend && npm install

fe-watch:
    cd examples/chat-app/frontend && npm run dev

fe-serve:
    cd examples/chat-app/frontend && npm run serve

# Formatting
format *flags:
    python3 scripts/format.py {{flags}}

format-check:
    python3 scripts/format.py --check

# Protocol
protocol:
    ./scripts/compile_protocol.sh

# Docs
docs *args:
    cd Docs && npm run {{args}}

# Meta
setup:
    git submodule update --init --recursive
    cd examples/chat-app/frontend && npm install

clean:
    rm -rf examples/chat-app/build examples/chat-app/builds
    rm -rf examples/chat-app/frontend/dist
    rm -rf examples/chat-app/frontend/src/generated

all: setup frontend build
