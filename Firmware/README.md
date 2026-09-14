# Mesh-NOW Node Firmware

A reference mesh node implementation using the Mesh-NOW library.

## What It Does

Each ESP32 is a standalone mesh node. It joins the mesh in WiFi STA mode over ESP-NOW and exposes a Web Serial API over USB. The web UI lives in `../Demo` and talks to any node you plug in over Web Serial.

## Structure

```text
Firmware/
├── main/               # ESP-IDF app (main.c, wifi_manager, serial_api)
├── sdkconfig.defaults  # Base config + per-target variants (*.esp32*)
└── CMakeLists.txt      # ESP-IDF project (pulls in ../Build)
```

The firmware depends on the library in `../Build` (component `Build`).

## Building

```bash
cd Firmware
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Or use the build script (builds all targets or a single `--target`):

```bash
python scripts/build.py --target esp32
```

## Usage

1. Flash one or more ESP32 nodes
2. Run the web UI (`cd Demo && npm run serve`)
3. Connect a node to the UI via Web Serial
4. Messages propagate through the mesh

## Documentation

See the [main Mesh-NOW docs](https://nellowtcs.me/Mesh-NOW/docs) for the library API and protocol specification.
