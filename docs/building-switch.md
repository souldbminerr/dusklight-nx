# Building for Switch

## Deps

- devkitPro devkitA64 + libnx
- NXVK (`libnvk.a`, Vulkan headers) in `portlibs/switch` or `NXVK_ROOT`
- Ninja, CMake 3.25+

## Build

```sh
export DEVKITPRO=/opt/devkitpro
cmake --preset switch-devkitA64-release
cmake --build --preset switch-devkitA64-release
```

NRO lands next to the elf in the build dir (`dusklight.nro`).

## Run

Copy to `sdmc:/switch/dusklight/dusklight.nro`. Disc image goes under
`sdmc:/switch/dusklight/`. Full takeover (not applet mode) required for
max APM configs.

## No SDL

The Switch path is native libnx, no SDL3 (faster, no buggy port):

| module | Switch behavior |
|---|---|
| borealis::io | native FILE/dirents impl |
| borealis::data | fixed `sdmc:/switch/dusklight` |
| borealis::disc | header-magic inspect, raw-ISO XXH3 verify |
| borealis::file_select | no dialogs, returns Canceled |
| borealis::http / ws | no backend, fail fast |
| borealis::update | Disabled, version parse kept |
| borealis::net | off, LiveSplit stubbed out |
| audio / input | audren / hid directly |

File picking on device is folder scan via `io::list`, not dialogs.

## Perf

APM Normal-mode configs only:

| | max | default |
|---|---|---|
| docked | 0x00010001 | 0x00010000 |
| handheld | 0x00020004 | 0x00020003 |

No clkrst, no FastLoad (parks GPU at 76MHz).

## Status

Proven: toolchain, libnx link, runtime + borealis-native TUs compile,
elf to nro.

Next: Dawn has no in-tree Switch backend (needs a `dawn-switch`-style
Vulkan fork wired to NXVK), Aurora needs native window/input/main
against libnx + `VK_NN_vi_surface`. Game code stays untouched behind
`DUSK_PLATFORM_SWITCH`.
