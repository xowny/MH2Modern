# Manhunt 2 Startup Code Map

This note records the measured startup path for `Manhunt2.exe` and the key code
sites that explain the game's extremely fast boot feel.

## Measured Critical Path

Runtime trace from `MH2Modern.log`:

- First window reached in about `23-25 ms`
- Pre-window file opens: `2`
- Pre-window reads: `1`
- Pre-window bytes read: `296`
- Pre-window DLL loads: `1`
- First window class: `Direct3DWindowClass`
- First window title: `Manhunt 2`

Observed pre-window sequence:

1. Load `user32.dll`
2. Open `global/pictures/legal_fr.tex`
3. Open `%LOCALAPPDATA%\\Rockstar Games\\Manhunt 2\\settings\\settings.dat`
4. Read `296` bytes
5. Create the first window

Observed post-window work:

- Load `d3d9.dll`
- Load precompiled shader blobs from `.\\shaders\\*.fxo`
- Initialize FMOD
- Load `GLOBAL/GAME.GXT`
- Load global texture/content files

Measured post-window bootstrap chain from targeted stack logging:

- first shader blob open returns through:
  - `Manhunt2.exe+0x22F05C`
  - `Manhunt2.exe+0x22F5DA`
  - `Manhunt2.exe+0x22F648`
  - `Manhunt2.exe+0x228E72`
  - `Manhunt2.exe+0x21B3D7`
  - `Manhunt2.exe+0x21B40B`
- first `GLOBAL/GAME.GXT` open returns through the same chain
- first `global/gmodelspc.tex` open returns through the same chain

Meaning:

- once the window exists and `d3d9.dll` is loaded, the first observed shader
  blob open climbs through both CRT file-opening code and a higher game-owned
  binary stream wrapper.
- this is the second startup phase, and it is deliberately kept off the
  pre-window critical path.
- static inspection shows:
  - `Manhunt2.exe+0x22F05A` calls the low-level open helper that reaches
    `CreateFileA`
  - `Manhunt2.exe+0x22F5D5` is an `_sopen_s`-style wrapper
  - `Manhunt2.exe+0x22F643` is a small default-parameter wrapper above it
  - `Manhunt2.exe+0x228E72` returns from the CRT mode parser that handles
    `a/r/w` plus `ccs=UTF-8|UTF-16LE|UNICODE`
  - `Manhunt2.exe+0x21B40B` is a higher `fopen`-style binary-read wrapper that
    uses mode string `rb` and default share mode `0x40`
- in other words, `0x22F05C -> 0x22F5DA -> 0x22F648` is generic CRT plumbing,
  not the actual engine stage by itself. The first clearly game-owned layer in
  the observed shader-blob path is the `0x21B3D7/0x21B40B` wrapper pair.

## Static Code Sites

### USER32 preload

- `Manhunt2.exe+0x0A768` pushes the `user32.dll` string
- `Manhunt2.exe+0x0A76D` calls `LoadLibraryW`
- Runtime stack confirms the load returns at `Manhunt2.exe+0x0A773`

String:

- `0x63976C` = `user32.dll`

Meaning:

- The game explicitly preloads `user32.dll` very early, before window creation.

### Window-class setup

The setup block lives in the `.secu` section:

- `Manhunt2.exe+0x3A7401` calls `LoadCursorW`
- `Manhunt2.exe+0x3A745D` calls `RegisterClassW`
- `Manhunt2.exe+0x3A77C9` calls `CreateWindowExW`
- Runtime stack confirms the first window call returns at `Manhunt2.exe+0x3A77CF`

String:

- `0x639790` = `Direct3DWindowClass`

Meaning:

- The window class is registered and the game window is created from the same
  compact startup block in `.secu`.
- This is the main reason boot feels instant: the window is created before
  renderer/content initialization.

### Lazy D3D9 loader

The renderer DLL is not part of the pre-window path. It is resolved lazily
after the window already exists.

Loader helper:

- `Manhunt2.exe+0x13EF0` checks whether the D3D9 module handle is already cached
- `Manhunt2.exe+0x13EFC` pushes the `d3d9.dll` string
- `Manhunt2.exe+0x13F01` calls `LoadLibraryW`
- Runtime stack confirms the load returns at `Manhunt2.exe+0x13F07`
- `Manhunt2.exe+0x140F0` calls the helper

Resolved exports in the same helper:

- `Direct3DCreate9`
- `D3DPERF_BeginEvent`
- `D3DPERF_EndEvent`
- `D3DPERF_SetMarker`
- `D3DPERF_SetRegion`
- `D3DPERF_QueryRepeatFrame`
- `D3DPERF_SetOptions`
- `D3DPERF_GetStatus`
- `CreateDXGIFactory`

Relevant strings:

- `0x63B1E0` = `d3d9.dll`
- `0x63B1D0` = `Direct3DCreate9`
- `0x63B1BC` = `D3DPERF_BeginEvent`
- `0x63B1F4` = `CreateDXGIFactory`

Meaning:

- The game intentionally defers renderer DLL work until after first window.
- It also uses a manual `LoadLibraryW + GetProcAddress` pattern instead of
  making D3D9 part of the initial import-time critical path.

### Shared post-window bootstrap stage

Targeted runtime stack logging shows the first shader blob, first `GAME.GXT`,
and first global texture file all funnel through the same post-window stage:

- `Manhunt2.exe+0x22F05C`
- `Manhunt2.exe+0x22F5DA`
- `Manhunt2.exe+0x22F648`

Meaning:

- after the first window and lazy D3D9 load, the engine transitions into a
  shared content bootstrap path instead of doing all heavyweight work up front.
- this is the actual “finish startup” stage that users do not perceive as part
  of launch latency because the window already exists.
- those three frames alone are still generic file-open machinery, so they
  should be treated as plumbing. The meaningful boundary is where the stack
  climbs above them into engine-owned callers.

### CRT file-open ladder vs engine-owned caller

The deeper shader-blob trace is:

- `Manhunt2.exe+0x22F05C`
- `Manhunt2.exe+0x22F5DA`
- `Manhunt2.exe+0x22F648`
- `Manhunt2.exe+0x228E72`
- `Manhunt2.exe+0x21B3D7`
- `Manhunt2.exe+0x21B40B`

What that means:

- `0x22F05C` is the low-level open helper that eventually reaches
  `CreateFileA`
- `0x22F5DA` / `0x22F648` are the `_sopen_s`-style wrapper path
- `0x228E72` is the CRT mode-string parser handling
  `ccs=UTF-8|UTF-16LE|UNICODE`
- `0x21B3D7` / `0x21B40B` are where the call path becomes interesting for the
  game itself

Static inspection of `0x21B40B` shows:

- it is part of a small `fopen`-style wrapper using mode string `rb`
- one static caller builds a path via format string `%s%s%s` immediately before
  the open
- static data used by that caller contains:
  - `0x66D4A0` = `.\shaders\`
  - `0x66D498` = `.fxo`
- that makes it a strong candidate for the precompiled shader-blob loader, and
  the path-building pattern matches the observed runtime open
  `.\shaders\Unlit_NoTexture.fxo`

### Proven shader-blob loader path

The strongest post-window proof so far is the first shader blob path.

Static facts from the engine-side caller:

- `Manhunt2.exe+0x208575` rebuilds `.\shaders\` from inline data at
  `0x66D4A0`
- `Manhunt2.exe+0x2085B8` rebuilds `.fxo` from inline data at `0x66D498`
- `Manhunt2.exe+0x2085DA` pushes format string `%s%s%s`
- `Manhunt2.exe+0x2085EF` calls the binary-read wrapper with mode string `rb`

Meaning:

- the game is not compiling shaders on boot
- it is constructing `.\shaders\` + shader-name + `.fxo`
- then loading the precompiled blob from disk in binary mode

That is a direct part of the startup “sauce”:

- renderer DLL work is delayed until after first window
- shader work is shipped as precompiled blobs
- startup only has to do cheap path construction plus file open/read

### Static post-window content table

`GAME.GXT` and `gmodelspc.tex` are not isolated one-off literals. They sit in a
static table in `.data` around `0x6A2F20`.

Representative entries:

- `0x6A2F44`: `0x006623D8`, `0x006623D8`, `0x00759744`
  - string `0x6623D8` = `global/gmodelspc.mdl`
- `0x6A2F50`: `0x006623C0`, `0x006623C0`, `0x007597F4`
  - string `0x6623C0` = `global/gmodelspc.tex`
- `0x6A2F98`: `0x00662318`, `0x00662318`, `0x007595AC`
  - string `0x662318` = `GAME.GXT`

What that means:

- post-window content bootstrap is at least partly table-driven
- each entry appears to contain:
  - one or two resource path pointers
  - one per-entry runtime state pointer/slot in zero-initialized `.data`

Important correction:

- those `0x007595xx-0x007597xx` values are not proven code pointers
- they land in the virtual-only part of `.data`, not in `.text`
- so the safer reading is “resource descriptor or runtime slot,” not
  “handler function”

Why it matters:

- startup content looks organized as a compact descriptor table
- that matches the observed behavior: after first window, the engine can walk a
  known list of core resources instead of doing broad discovery or archive
  scans
- this is another concrete part of the fast-boot design

Important boundary:

- `0x22F05C -> 0x22F648` explains *how* the file gets opened
- `0x21B3D7 -> 0x21B40B` is the first layer that starts explaining *why* this
  particular startup asset is being opened

## Why It Boots So Fast

The game is optimized for **time-to-first-window**, not for
**time-to-fully-initialized-engine**.

What Rockstar got right:

- almost no pre-window I/O
- no launcher
- no network/bootstrap service layer
- no shader compilation on boot
- renderer DLL loaded lazily
- audio/content initialization deferred until after the window exists

That is the whole trick. It is a very aggressive critical-path
cut.
