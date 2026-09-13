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

The recommended way is the ESP-IDF component manager. Add this to your project's `idf_component.yml`:

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

The root [library.json](/library.json) describes the library (`mesh_now`, ESP-IDF and Arduino frameworks):

```ini
lib_deps =
    https://github.com/NellowTCS/Mesh-NOW.git
```

### Arduino

The root [library.properties](/library.properties) registers `mesh_now` with the Arduino Library Manager.

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
├── Demo/                    # TypeScript chat GUI (baked into the firmware)
├── Docs/                    # Documentation (docmd)
├── Tests/                   # Host unit tests
├── scripts/                 # Python + shell drivers
├── mesh_now.ksy             # Wire-format spec (Kaitai)
├── library.json             # PlatformIO library manifest
├── library.properties       # Arduino library manifest
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
