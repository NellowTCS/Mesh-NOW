---
name: ESP32 Target Support
about: Request support for a new ESP32 variant or report target-specific issues
title: "[TARGET] "
labels: ["hardware-support", "esp32"]
assignees: ""
---

## ESP32 Target Information

**Target:** (e.g., ESP32-H2, ESP32-P4, custom board)
**Architecture:** (e.g., RISC-V, Xtensa)
**RAM:** (e.g., 512KB)
**Flash:** (e.g., 4MB)
**Special Features:** (e.g., Bluetooth, 802.15.4)

## Request Type

- [ ] Add support for new ESP32 variant
- [ ] Fix existing target support
- [ ] Optimize configuration for existing target
- [ ] Custom board support

## Current Status

If this is for an existing supported target, what's working and what isn't?

**Working:**

- [ ] Build completes successfully
- [ ] Firmware flashes without errors
- [ ] Device boots and starts WiFi AP
- [ ] Web interface accessible
- [ ] ESP-NOW communication works
- [ ] Multi-device mesh networking

**Not Working:**

- [ ] Build fails with errors
- [ ] Flash process fails
- [ ] Device doesn't boot properly
- [ ] WiFi AP not created
- [ ] Web interface inaccessible
- [ ] ESP-NOW not functioning
- [ ] Mesh networking issues

## Technical Details

**ESP-IDF Support:** (Does ESP-IDF support this target?)
**Toolchain:** (What toolchain is required?)
**Memory Constraints:** (Any special memory considerations?)
**Hardware Limitations:** (Missing features compared to other ESP32s?)

## Configuration Requirements

If you know what configuration changes might be needed:

```cpp
# sdkconfig changes needed
CONFIG_EXAMPLE=y
```

## Testing

For new target requests:

- [ ] I have access to this hardware for testing
- [ ] I can help with testing and validation
- [ ] I only need the configuration, no hardware access

For existing target issues:

- [ ] I have tested on multiple devices of this type
- [ ] Issue is reproducible across different boards
- [ ] I have compared with working targets

## Additional Context

- **Documentation:** Links to official ESP32 target documentation
- **Datasheet:** Links to technical specifications
- **Development Board:** Specific board model and vendor
- **Use Case:** What you plan to use this target for

## Checklist

- [ ] I have checked if this target is already supported
- [ ] I have verified ESP-IDF supports this target
- [ ] I have searched for existing issues about this target
- [ ] I understand this may require testing and validation time
