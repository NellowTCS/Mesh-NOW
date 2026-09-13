# Mesh-NOW Node Firmware

A reference implementation of a mesh chat application using the Mesh-NOW library.

## What It Does

Each ESP32 runs a WiFi access point, serves a web-based chat interface over HTTP, and communicates with nearby nodes over ESP-NOW. Connect to any node's WiFi, open a browser, and start chatting.

## Structure

```text
Firmware/
├── main/               # ESP-IDF app (main.c, wifi_manager, serial_api)
├── sdkconfig.defaults  # Base config + per-target variants (*.esp32*)
└── CMakeLists.txt      # ESP-IDF project (pulls in ../Build)
```

The firmware depends on the library in `../Build` (component `Build`) and the
embedded chat GUI in `../Demo`, which is built and converted to C headers
(`Firmware/main/*_html.h`, `*_js.h`, `*_css.h`) before compilation.

## Building

```bash
cd Firmware
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

To build the web GUI into the firmware:

```bash
python3 scripts/build_frontend.py
python3 scripts/embed_frontend.py
cd Firmware && idf.py build
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

See the [main Mesh-NOW docs](https://nellowtcs.me/Mesh-NOW/docs) for the library API and protocol specification.
