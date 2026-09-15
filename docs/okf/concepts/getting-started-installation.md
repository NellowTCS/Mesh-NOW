---
type: concept
title: Installation
description: "Set up ESP-IDF and integrate the Mesh-NOW library into your project."
source: "https://NellowTCS.github.io/Mesh-NOW/getting-started/installation/"
path: /getting-started/installation/
updated: 2026-09-15
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-09-15T19:14:56.553Z"
---
---
title: "Installation"
description: "Set up ESP-IDF and integrate the Mesh-NOW library into your project."
---

Three ways to use Mesh-NOW: as an ESP-IDF component (via the component manager or `EXTRA_COMPONENT_DIRS`), as a PlatformIO library, or by copying the source directly.

::: tabs

::: tab "ESP-IDF Component"

The recommended path is the ESP-IDF component manager. Add this to your project's `idf_component.yml`:

```yaml
dependencies:
  mesh_now:
    git: https://github.com/NellowTCS/Mesh-NOW.git
    path: Build
```

The `path` field resolves the component subdirectory, and the library registers under the name `mesh_now`.

Alternatively, point `EXTRA_COMPONENT_DIRS` at `Build/` in your project's `CMakeLists.txt`. The library is self-contained (the vendored mpack is compiled in) and registers under the component name `Build`, which is just the directory name:

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

The repository root carries a `library.json` manifest that matches the ESP-IDF and Arduino frameworks; the Arduino framework is used through PlatformIO or Git Submodule rather than the Arduino Library Manager (see the [README](https://github.com/NellowTCS/Mesh-NOW#arduino)).

::: /tab

::: tab "Manual"

Copy the `Build/` directory into your project, renaming it to `mesh_now`:

```bash
cp -r /path/to/Mesh-NOW/Build your-project/components/mesh_now
```

That keeps the component registered under the name `mesh_now`. (When used in-place via `EXTRA_COMPONENT_DIRS`, the component is named `Build` because that is the directory name.)

Make sure your build system includes the component directory.

::: /tab

::: /tabs

## Prerequisites

::: callout info title:"ESP-IDF"
Mesh-NOW requires ESP-IDF v5.5 or later. The code targets the v5.x ESP-NOW APIs and `esp_timer`, so v4.x is not supported.
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

The header exposes all public types, constants, and function declarations. It carries an `extern "C"` wrapper for C++ projects.

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
