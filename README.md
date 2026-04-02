# MH2Modern

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)


A modern compatibility and quality-of-life patch for the PC version of Manhunt 2.

MH2Modern focuses on old technical behavior that does not age well on modern Windows: mouse input, fullscreen handling, frame pacing, audio compatibility, and a few system-level habits the game should not still have.

Nexus Mods: https://www.nexusmods.com/manhunt2/mods/52

## Compatibility And Safety

**Primary target:** PC release of Manhunt 2  
**Loader:** requires an ASI loader setup  
**Network traffic:** none  
**Telemetry:** none  
**Permanent OS changes:** none  
**Admin rights:** not required to run the mod, but Windows may require elevated permissions to copy files into `Program Files`  
**Files used by the mod:** `MH2Modern.asi`, `MH2Modern.ini`, and optional log or dump files if you enable diagnostics  

You can disable the mod instantly by removing `MH2Modern.asi` from the game folder.  
You can fully uninstall it by removing both `MH2Modern.asi` and `MH2Modern.ini`.

## What MH2Modern Does

- Adds raw mouse input for the DirectInput mouse path.
- Improves frame pacing with a steadier 60 FPS limiter path.
- Applies a lower background frame cap when the game is unfocused.
- Improves old D3D9 fullscreen presentation behavior.
- Hardens fragile fullscreen recovery and lost-device handling.
- Fixes cursor clip behavior when alt-tabbing.
- Stops the game from changing Windows mouse and accessibility settings.
- Applies FMOD compatibility fixes for modern systems.
- Removes proven redundant frontend audio setter spam.
- Can write a crash dump after a hard crash for easier troubleshooting.

## Installation

1. Make sure your game has a working ASI loader.
2. Copy `MH2Modern.asi` and `MH2Modern.ini` into your Manhunt 2 game folder.
3. Launch the game.
4. Leave the default settings on unless you are troubleshooting.

The included INI is already organized around recommended defaults.  
All logging and debug options are grouped at the end of the file and are off by default.

## Configuration

`MH2Modern.ini` is organized into normal user-facing sections:

- Stability And Compatibility
- Graphics
- Mouse And Input
- Audio
- Frame Pacing
- Diagnostics

Most users should not need to touch anything outside the frame limit or background limit settings.

## Included Fixes

### Stability and compatibility

- Prevents the game from pinning itself to CPU 0 during startup.
- Enables modern DPI awareness behavior.
- Requests a 1 ms timer for better pacing.
- Preserves crash dump capture for hard crashes.

### Graphics

- Forces a safer fullscreen presentation interval policy.
- Cleans up fragile D3D9 reset parameters during display recovery.
- Reduces CPU waste in old lost-device polling loops.

### Mouse and input

- Uses raw mouse input for mouse users.
- Releases and restores the fullscreen cursor clip correctly on focus changes.
- Blocks the game from modifying Windows-wide mouse and accessibility settings.

### Audio

- Applies FMOD startup compatibility fixes.
- Cleans up redundant frontend music and audio setter spam.
- Uses a safer stereo fallback path on modern systems.
- Injects a safer DSP buffer setup when the original game leaves it underspecified.

### Frame pacing

- Uses the MH2Modern frame limiter path.
- Applies hybrid sleep/spin pacing for steadier frametimes.
- Drops to a lower frame cap while unfocused.

## Build From Source

Requirements:

- Visual Studio 2022 Build Tools with x86 C++ tools
- A Windows 10 or Windows 11 SDK with x86 libraries
- MinGW g++ for the test build used by `test.bat`

Build commands:

```bat
test.bat
build.bat
```

Expected output:

- `build\MH2Modern.asi`
- `build\MH2Modern.ini`

## Project Layout

- `src/` — runtime hooks and patch logic
- `tests/` — lightweight regression tests
- `MH2Modern.ini` — user-facing configuration
- `build.bat` — x86 release build script
- `test.bat` — test build and test runner

## Scope

MH2Modern is meant to coexist with mods that solve different problems, such as controller-specific or widescreen-specific work.

This project is focused on modernizing fragile old PC behavior inside the original executable path. It is not trying to redesign the game.

## License

Released under the MIT License. See [LICENSE](LICENSE).
