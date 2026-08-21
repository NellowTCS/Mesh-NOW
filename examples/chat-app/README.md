# Mesh-NOW Chat App

A reference implementation of a mesh chat application using the Mesh-NOW library.

## What It Does

Each ESP32 runs a WiFi access point, serves a web-based chat interface over HTTP, and communicates with nearby nodes over ESP-NOW. Connect to any node's WiFi, open a browser, and start chatting.

## Structure

```
chat-app/
├── main/           # ESP-IDF application (main.c, wifi_manager, web_server)
├── frontend/       # TypeScript web UI (webpack, bundled into firmware)
├── configs/        # Per-target sdkconfig files
├── scripts/        # Build, flash, and utility scripts
└── CMakeLists.txt  # ESP-IDF project config
```

## Building

```bash
cd examples/chat-app
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Or use the interactive build script:

```bash
python scripts/build.py --with-frontend
```

## Usage

1. Flash the firmware to your ESP32
2. Connect to `MESH-NOW-XXXXXXXX` WiFi (password: `password`)
3. Open `http://192.168.4.1`
4. Start chatting

## Documentation

See the [main Mesh-NOW docs](../../Docs/) for the library API and protocol specification.
