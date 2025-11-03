# Mesh-NOW

> This is very WIP! The messages are sent via the default 128 bit encryption of ESP-NOW but there is no PMK/spoofing protection.

A serverless mesh network chat application using ESP32 devices and the ESP-NOW protocol. Direct device-to-device communication without requiring a central WiFi router or internet connection.

## Features

- **Serverless Mesh Network**: Direct ESP32 to ESP32 communication using ESP-NOW
- **Web Interface**: Modern responsive web UI for chat functionality
- **Automatic Peer Discovery**: Devices automatically discover and connect to nearby ESP32s
- **Real-time Messaging**: Low-latency message delivery across the mesh
- **WiFi Access Point**: Each device creates its own WiFi hotspot for web access
- **Message Queuing**: Reliable message handling with FreeRTOS queues

## Project Structure

```bash
Mesh-NOW/
├── main/
│   ├── main.c              # Main application code with ESP-NOW mesh
│   └── CMakeLists.txt      # Component build configuration
├── Frontend/
│   └── index.html          # Web interface (embedded in main.c)
├── configs/
│   ├── sdkconfig.esp32     # ESP32 configuration
│   ├── sdkconfig.esp32s2   # ESP32-S2 configuration
│   ├── sdkconfig.esp32s3   # ESP32-S3 configuration
│   ├── sdkconfig.esp32c3   # ESP32-C3 configuration
│   └── sdkconfig.esp32c6   # ESP32-C6 configuration
├── scripts/
│   ├── build.sh            # Interactive build script
│   ├── build_all_targets.sh # Multi-target build script
│   └── test_targets.sh     # Configuration test script
├── builds/                 # Build artifacts (generated)
├── CMakeLists.txt          # Main project configuration
└── README.md              # This file
```

## Supported ESP Targets

| Target   | Architecture       | RAM   | Status       |
| -------- | ------------------ | ----- | ------------ |
| ESP32    | Dual-core Xtensa   | 520KB | ✅ Supported |
| ESP32-S2 | Single-core Xtensa | 320KB | ✅ Supported |
| ESP32-S3 | Dual-core Xtensa   | 512KB | ✅ Supported |
| ESP32-C3 | Single-core RISC-V | 400KB | ✅ Supported |
| ESP32-C6 | Single-core RISC-V | 512KB | ✅ Supported |

## How It Works

1. **WiFi Access Point**: Each device creates a WiFi AP (SSID: "ESP32-Chat")
2. **Web Interface**: Users connect to the AP and browse to <http://192.168.4.1>
3. **ESP-NOW Mesh**: Messages are broadcast via ESP-NOW to all nearby devices
4. **Peer Discovery**: Devices discover each other using UDP broadcasts
5. **Message Routing**: Messages propagate through the mesh network

## Installation and Setup

### Prerequisites

- ESP-IDF v5.5.1 or later
- Python 3.7+
- Git

### Install ESP-IDF

```bash
# Clone ESP-IDF
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf

# Install for ESP32
./install.sh esp32

# Set up environment
. ./export.sh
```

### Build the Project

```bash
# Clone this repository
git clone https://github.com/NellowTCS/Zephyr-NOW.git
cd Zephyr-NOW
```

#### Build for Specific Target

```bash
# Set target (esp32, esp32s2, esp32s3, esp32c3, esp32c6)
idf.py set-target esp32

# Apply target-specific configuration
cp configs/sdkconfig.esp32 sdkconfig.defaults

# Build the project
idf.py build
```

#### Interactive Target Selection

```bash
# Use interactive build script
./scripts/build.py --with-frontend
```

#### Build for All Targets

```bash
# Build for all supported ESP32 variants
./scripts/build_all_targets.py
```

### Flash to ESP32

```bash
# Connect ESP32 via USB and flash
idf.py -p /dev/ttyUSB0 flash monitor

# Or auto-detect port
idf.py flash monitor
```

### Flashing Manually

#### ESP32C3

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 --baud 115200 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x0 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0x10000 mesh-now.bin
```

## Usage

1. Flash the firmware to your ESP32 device
2. The device will create a WiFi AP with:
   - **SSID**: MESH-NOW-[8 character string from MAC address]
   - **Password**: password
3. Connect to the WiFi network
4. Open your browser and go to: <http://192.168.4.1>
5. Start chatting! Messages will be broadcast to all devices in range

## Configuration

### Application Settings

Edit `main/main.c` to customize:

- `WIFI_SSID`: WiFi AP name (default: "MESH-NOW-[8 character string from MAC address]")
- `WIFI_PASS`: WiFi AP password (default: "password")
- `WIFI_CHANNEL`: WiFi channel (default: 1)
- `ESPNOW_CHANNEL`: ESP-NOW channel (default: 1)

### Target-Specific Configurations

Each ESP32 variant has optimized settings in the `configs/` directory:

- `configs/sdkconfig.esp32`: ESP32 dual-core (520KB RAM)
- `configs/sdkconfig.esp32s2`: ESP32-S2 single-core (320KB RAM)
- `configs/sdkconfig.esp32s3`: ESP32-S3 dual-core (512KB RAM) with PSRAM
- `configs/sdkconfig.esp32c3`: ESP32-C3 RISC-V (400KB RAM)
- `configs/sdkconfig.esp32c6`: ESP32-C6 RISC-V (512KB RAM) with 802.15.4

### Build Scripts

- `scripts/build.sh`: Interactive target selection and build
- `scripts/build_all_targets.sh`: Build for all targets automatically
- `scripts/test_targets.sh`: Quick test of all target configurations

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
