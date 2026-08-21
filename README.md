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
- **Payload encryption** - optional XOR cipher with up to 32-byte keys
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

```bash
git clone https://github.com/NellowTCS/Mesh-NOW.git
cd Mesh-NOW
```

Add to your project's `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS "/path/to/Mesh-NOW/components")
```

Or copy `components/mesh_now/` into your project's `components/` directory.

### PlatformIO

```ini
lib_deps =
    https://github.com/NellowTCS/Mesh-NOW.git
```

### Requirements

- ESP-IDF v5.5.1+ (v4.x partially supported)
- Python 3.7+

## Project Structure

```text
Mesh-NOW/
├── components/mesh_now/    # The library (use this)
│   ├── include/
│   │   ├── mesh_now.h
│   │   └── message_queue.h
│   └── src/
│       ├── mesh_now.c
│       └── message_queue.c
├── Docs/                   # Documentation (docmd)
├── examples/
│   └── chat-app/           # Reference chat application
├── library.json            # PlatformIO library manifest
└── LICENSE                 # MIT
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
