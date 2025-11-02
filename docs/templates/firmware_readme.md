# Mesh-NOW Firmware for {TARGET}

## Files

- `mesh-now.bin` - Main application firmware
- `bootloader.bin` - ESP32 bootloader
- `partition-table.bin` - Partition table
- `flash.py` - Automated flash script (Python)

## Manual Flashing

```bash
esptool.py --chip {TARGET} write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 mesh-now.bin
```

## Target Specifications

- **Architecture:** {ARCHITECTURE}
- **RAM:** {RAM}
- **Special Features:** Target-specific optimizations applied

## Quick Start

1. Connect your ESP32 to your computer
2. Put the ESP32 in download mode (hold BOOT, press RESET, release BOOT)
3. Run: `python flash.py`
4. Reset your ESP32 (press RESET button)
5. Connect to "MESH-NOW" WiFi network (password: "password")
6. Open <http://192.168.4.1> in your browser

## Troubleshooting

- **Can't connect to ESP32:** Check USB cable and drivers
- **Flash fails:** Ensure ESP32 is in download mode
- **Can't see WiFi:** Wait 10-20 seconds after reset
- **Web page won't load:** Check you're connected to MESH-NOW network
