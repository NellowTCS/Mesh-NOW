---
title: "Installation"
description: "Set up ESP-IDF and integrate the Mesh-NOW library into your project."
---

Three ways to use Mesh-NOW: as an ESP-IDF component, as a PlatformIO library, or by copying the source directly.

::: tabs

::: tab "ESP-IDF Component"

Add Mesh-NOW to your project's `components/` directory or use `EXTRA_COMPONENT_DIRS`:

```bash
# Clone into your project's components directory
git clone https://github.com/NellowTCS/Mesh-NOW.git components/mesh-now
```

Or in your project `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS "/path/to/Mesh-NOW/components")
```

Then depend on it in your component:

```cmake
idf_component_register(SRCS "main.c"
                       REQUIRES mesh_now)
```

::: /tab

::: tab "PlatformIO"

Add to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/NellowTCS/Mesh-NOW.git
```

Or use the library manifest directly:

```json
{
    "name": "mesh_now",
    "version": "1.0.0",
    "platforms": ["espressif32"],
    "frameworks": ["arduino", "espidf"]
}
```

::: /tab

::: tab "Manual"

Copy the `components/mesh_now/` directory into your project:

```bash
cp -r /path/to/Mesh-NOW/components/mesh_now your-project/components/
```

Ensure your build system includes the component directory.

::: /tab

::: /tabs

## Prerequisites

::: callout info title:"ESP-IDF"
Mesh-NOW requires ESP-IDF v5.5 or later. ESP-IDF v4.x is partially supported via compatibility macros.
::: /callout

### Install ESP-IDF

```bash
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32
. ./export.sh
```

### Verify

```bash
idf.py --version
# Should output v5.5.1 or later
```

## Header Include

```c
#include "mesh_now.h"
```

The header provides all public types, constants, and function declarations. The `extern "C"` wrapper is included for C++ compatibility.

## Next Steps

::: grids
::: grid
::: button "Quick Start" ./quickstart.md icon:play
::: /grid

::: grid
::: button "Core Concepts" ./concepts.md icon:book
::: /grid

::: grid
::: button "Message Format" ../guide/message-format.md icon:file-text
::: /grid
::: /grids
