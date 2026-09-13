---
title: "Installation"
description: "Set up ESP-IDF and integrate the Mesh-NOW library into your project."
---

Four ways to use Mesh-NOW: as an ESP-IDF component (via the component manager or `EXTRA_COMPONENT_DIRS`), as a PlatformIO or Arduino library, or by copying the source directly.

::: tabs

::: tab "ESP-IDF Component"

The recommended way is the ESP-IDF component manager. Add this to your project's `idf_component.yml`:

```yaml
dependencies:
  mesh_now:
    git: https://github.com/NellowTCS/Mesh-NOW.git
    path: Build
```

The `path` field resolves the component subdirectory, and the library registers under the name `mesh_now`.

Alternatively, point `EXTRA_COMPONENT_DIRS` at `Build/` in your project's `CMakeLists.txt`. The library is self-contained (vendored mpack is compiled into it) and registers under the component name `Build`, the directory name:

```cmake
set(EXTRA_COMPONENT_DIRS
    "/path/to/Mesh-NOW/Build")
```

Then depend on it in your component:

```cmake
idf_component_register(SRCS "main.c"
                       REQUIRES Build)
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

::: tab "Arduino"

The root `library.properties` registers `mesh_now` with the Arduino ecosystem:

```bash
arduino-cli lib install --git-url https://github.com/NellowTCS/Mesh-NOW.git
```

::: /tab

::: tab "Manual"

Copy the `Build/` directory into your project, renaming it to `mesh_now`:

```bash
cp -r /path/to/Mesh-NOW/Build your-project/components/mesh_now
```

This keeps the component registered under the `mesh_now` name. (When used
in-place via `EXTRA_COMPONENT_DIRS`, the component is named `Build` because
that is the directory name.)

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
