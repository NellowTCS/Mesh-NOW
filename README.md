# Zephyr-NOW

A mesh network chat system powered by ESP-NOW and Zephyr RTOS. Like Meshtastic, but using ESP-NOW for local mesh networking.

## Features

- **ESP-NOW Mesh Networking**: Direct device-to-device communication without router
- **Web Interface**: Connect via WiFi AP and use browser-based chat
- **Auto Discovery**: Devices automatically discover each other via UDP broadcast
- **Multi-Device Support**: Messages propagate through the mesh network
- **Low Power**: ESP-NOW is energy efficient for battery-powered nodes

## Supported ESP Targets

| Target | Architecture | RAM | Status |
|--------|-------------|-----|---------|
| ESP32 | Dual-core Xtensa | 520KB | ✅ Supported |
| ESP32-S2 | Single-core Xtensa | 320KB | ✅ Supported |
| ESP32-S3 | Dual-core Xtensa | 512KB | ✅ Supported |
| ESP32-C3 | Single-core RISC-V | 400KB | ✅ Supported |
| ESP32-C6 | Single-core RISC-V | 512KB | ✅ Supported |

## How It Works

1. **WiFi Access Point**: Each device creates a WiFi AP (SSID: "ESP32-Chat")
2. **Web Interface**: Users connect to the AP and browse to <http://192.168.4.1>
3. **ESP-NOW Mesh**: Messages are broadcast via ESP-NOW to all nearby devices
4. **Peer Discovery**: Devices discover each other using UDP broadcasts
5. **Message Routing**: Messages propagate through the mesh network

## Building

### Prerequisites

```bash
# Install west
pip install west

# Clone the repository
git clone https://github.com/NellowTCS/Zephyr-NOW.git
cd Zephyr-NOW

# Initialize workspace
west init -l .
west update
```

### Build for Specific Target

```bash
# ESP32
west build -b esp32_devkitc_wroom

# ESP32-S2
west build -b esp32s2_saola

# ESP32-S3
west build -b esp32s3_devkitm

# ESP32-C3
west build -b esp32c3_devkitm
```

### Build All Targets

```bash
./build_all.sh
```

### Flash

```bash
west flash
```

## Usage

1. Flash the firmware to your ESP32 device
2. The device will create a WiFi AP with:
   - **SSID**: ESP32-Chat
   - **Password**: password
3. Connect to the WiFi network
4. Open your browser and go to: http://192.168.4.1
5. Start chatting! Messages will be broadcast to all devices in range

## Configuration

Edit `src/main.c` to customize:

- `WIFI_SSID`: WiFi AP name (default: "ESP32-Chat")
- `WIFI_PASS`: WiFi AP password (default: "password")
- `WIFI_CHANNEL`: WiFi channel (default: 1)
- `ESPNOW_CHANNEL`: ESP-NOW channel (default: 1)
- `UDP_PORT`: Discovery port (default: 8888)

## Architecture

```text
┌─────────────────┐
│   Web Browser   │
│  (User Device)  │
└────────┬────────┘
         │ HTTP
         ▼
┌─────────────────┐     ESP-NOW Mesh     ┌─────────────────┐
│   ESP32 Node 1  │◄────────────────────►│   ESP32 Node 2  │
│  WiFi AP + HTTP │                      │  WiFi AP + HTTP │
└─────────────────┘                      └─────────────────┘
         ▲                                        ▲
         │                                        │
         │            ESP-NOW Mesh               │
         │                                        │
         └────────────┐      ┌───────────────────┘
                      ▼      ▼
              ┌─────────────────┐
              │   ESP32 Node 3  │
              │  WiFi AP + HTTP │
              └─────────────────┘
```

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Pull requests are welcome! For major changes, please open an issue first to discuss what you would like to change.
