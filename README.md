# Mesh-NOW

> Lightweight mesh networking protocol for ESP32 using ESP-NOW.

Mesh-NOW is a protocol and library that turns any ESP32 into a mesh networking node. Device-to-device communication over ESP-NOW with automatic peer discovery, multi-hop routing, message queuing, and optional payload encryption.

## Features

- **Serverless** - direct ESP32-to-ESP32 communication via ESP-NOW
- **Auto-discovery** - peers found automatically via periodic beacons
- **Multi-hop routing** - messages relay through intermediate nodes (configurable TTL)
- **7 message types** - beacon, chat, direct, ACK, group, presence, typing
- **Reliable delivery** - ACK-based retransmission for direct messages (3 retries, 2s timeout)
- **Group messaging** - scoped broadcasts with configurable group IDs
- **Payload encryption** - optional AES-128-GCM with a 16-byte key
- **FreeRTOS integration** - message queue, pinned tasks, callback support
- **5 ESP32 targets** - ESP32, S2, S3, C3, C6
- **Web installer** - flash node firmware from the browser with esptool-js

## Web Installer

The [web installer](/Demo/flasher.html) flashes firmware to a node over Web
Serial, without installing esptool or building anything. Pick a firmware
version and target chip, plug in the node, and flash. Each tagged release
publishes to GitHub Pages at [https://nellowtcs.github.io/Mesh-NOW/flasher.html](https://nellowtcs.github.io/Mesh-NOW/flasher.html), maintained by the
`static-site` workflow:

- [https://nellowtcs.github.io/Mesh-NOW](https://nellowtcs.github.io/Mesh-NOW) - chat UI
- [https://nellowtcs.github.io/Mesh-NOW/flasher.html](https://nellowtcs.github.io/Mesh-NOW/flasher.html) - web installer
- [https://nellowtcs.github.io/Mesh-NOW/docs](https://nellowtcs.github.io/Mesh-NOW/docs) - documentation site

## Quick Example

```c
#include "mesh_now.h"

static void on_message(const mesh_message_t *msg) {
    printf("Received: %s\n", msg->message);
}

void app_main(void) {
    mesh_now_init();
    mesh_now_set_receive_callback(on_message);
    mesh_now_send_broadcast("Hello, mesh!");
}
```

## Supported Targets

| Target | Architecture | RAM | Status |
| :----- | :----------- | :-- | :----- |
| ESP32 | Dual-core Xtensa | 520KB | Supported |
| ESP32-S2 | Single-core Xtensa | 320KB | Supported |
| ESP32-S3 | Dual-core Xtensa | 512KB | Supported |
| ESP32-C3 | Single-core RISC-V | 400KB | Supported |
| ESP32-C6 | Single-core RISC-V | 512KB | Supported |

## Installation

### ESP-IDF Component

The recommended way is the ESP-IDF component manager. Mesh-NOW is published to
the [ESP Component Registry](https://components.espressif.com) as
`nellowtcs/mesh_now`. Add this to your project's `idf_component.yml`:

```yaml
dependencies:
  nellowtcs/mesh_now: "^1.0.0"
```

Or install it with the CLI:

```bash
idf.py add-dependency "nellowtcs/mesh_now"
```

For local development you can also point the component manager at this
repository:

```yaml
dependencies:
  mesh_now:
    git: https://github.com/NellowTCS/Mesh-NOW.git
    path: Build
```

The `path` field resolves the component subdirectory; the library registers under the name `mesh_now`.

Alternatively, point `EXTRA_COMPONENT_DIRS` at `Build/` from your project's `CMakeLists.txt`. The library registers under the component name `Build` (the directory name), and is self-contained, vendored mpack is compiled into it:

```cmake
set(EXTRA_COMPONENT_DIRS
    "/path/to/Mesh-NOW/Build")
```

Then `REQUIRES Build` from your own components.

Or copy `Build/` into your project's `components/` directory (renamed to `mesh_now`):

```bash
cp -r /path/to/Mesh-NOW/Build your-project/components/mesh_now
```

### PlatformIO

The root [library.json](/library.json) describes the library (`mesh_now`, ESP-IDF and Arduino frameworks). It is published to the [PlatformIO Registry](https://registry.platformio.org) as `NellowTCS/Mesh-NOW`:

```ini
lib_deps = NellowTCS/Mesh-NOW
```

### Arduino

The [Arduino Library Manager](https://www.arduino.cc/reference/en/libraries/) is not supported. The library ships as an ESP-IDF component under `Build/`, and `arduino-lint` requires a root `src/` folder plus a top-level `mesh_now.h` for Library Manager submission, which is incompatible with that layout.

To use `mesh_now` from the Arduino ecosystem, build it through [PlatformIO](#platformio): the [library.json](/library.json) manifest declares the Arduino framework, so any `esp32` PlatformIO board project can just add `lib_deps = NellowTCS/Mesh-NOW`. A [library.properties](/library.properties) metadata file is kept at the root for tools that read it, but it is not registered with the Library Manager. You *can* still use it via git submodule or something, but it's not as easy as I would have liked :/

### Requirements

- ESP-IDF v5.5.1+ (v4.x partially supported)
- Python 3.7+

## Project Structure

```text
Mesh-NOW/
├── Build/                   # The mesh library (ESP-IDF component "Build")
│   ├── include/             #   public headers (mesh_now.h, message_queue.h)
│   ├── src/                 #   core, net, codec, crypto, queue
│   └── vendor/mpack/        #   vendored MessagePack library
├── Firmware/                # Reference chat-app firmware (ESP-IDF project)
├── Demo/                    # Web UI (Vite MPA): chat GUI + web installer
│   ├── index.html           #   chat page (Web Serial)
│   └── flasher.html         #   web installer page (esptool-js)
├── Docs/                    # Documentation (docmd)
├── Tests/                   # Host unit tests
├── scripts/                 # Python + shell drivers
├── mesh_now.ksy             # Wire-format spec (Kaitai)
├── library.json             # PlatformIO library manifest
├── library.properties       # Arduino metadata (not on Library Manager)
└── LICENSE                  # MIT
```

## Documentation

Full documentation is on the Docs [site](https://nellowtcs.me/Mesh-NOW), built with [docmd](https://docmd.io):

```bash
cd Docs
npm install
npm run dev
```

Or read the source markdown directly in `Docs/docs/`.

## Architecture

```
┌─────────────────────────────────────────────┐
│              Application Layer              │
│         (your code, callbacks, UI)          │
├─────────────────────────────────────────────┤
│           Mesh-NOW Library (C)              │
│  Peer Mgmt │ Routing │ Reliability │ Queue  │
├─────────────────────────────────────────────┤
│              ESP-NOW Transport              │
│        (Espressif device-to-device)         │
└─────────────────────────────────────────────┘
```

## License

MIT License - see [LICENSE](LICENSE) file.

## Contributing

Pull requests welcome. For major changes, open an issue first.
