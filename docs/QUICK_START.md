# Mesh-NOW Quick Start Guide

## What You Need

- ESP32 development board (any variant)
- USB cable
- Computer with Python installed

## Installation Steps

1. **Install esptool.py**

   ```bash
   python scripts/install_esptool.py
   ```

2. **Download firmware**

   - Choose the right firmware for your ESP32 variant
   - Extract the downloaded package

3. **Connect ESP32**

   - Connect ESP32 to computer via USB
   - Put ESP32 in download mode (hold BOOT, press RESET, release BOOT)

4. **Flash firmware**

   ```bash
   cd firmware/your-esp32-variant/
   python flash.py
   ```

5. **Connect to mesh network**
   - Reset your ESP32 (press RESET button)
   - Connect to "MESH-NOW" WiFi network
   - Password: "password"
   - Open browser to <http://192.168.4.1>

## Troubleshooting

- **Can't connect to ESP32:** Check USB cable and drivers
- **Flash fails:** Ensure ESP32 is in download mode
- **Can't see WiFi:** Wait 10-20 seconds after reset
- **Web page won't load:** Check you're connected to MESH-NOW network

## Multiple Devices

To create a mesh network:

1. Flash multiple ESP32 devices with Mesh-NOW
2. Power them on - they'll automatically discover each other
3. Connect to any device's WiFi network
4. Messages sent from any device appear on all connected web interfaces

## Supported ESP32 Variants

- **ESP32** - Original dual-core Xtensa (520KB RAM)
- **ESP32-S2** - Single-core Xtensa with native USB (320KB RAM)
- **ESP32-S3** - Dual-core Xtensa with PSRAM support (512KB RAM)
- **ESP32-C3** - Single-core RISC-V with Bluetooth LE (400KB RAM)
- **ESP32-C6** - Single-core RISC-V with 802.15.4 + BLE (512KB RAM)

## Advanced Usage

- **Custom flashing:** Use `esptool.py` directly for custom flash configurations
- **Batch flashing:** Use the universal flash script for multiple devices
- **Development:** See the main repository for build instructions

Enjoy your serverless mesh chat network!
