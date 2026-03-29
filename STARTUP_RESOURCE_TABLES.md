# Manhunt 2 Startup Resource Tables

This note records the decoded startup-oriented resource descriptor tables found
in `.data`.

## Core Shape

Two adjacent tables around `0x6A2C20` and `0x6A2F20` decode cleanly as
12-byte entries:

1. `path_a`
2. `path_b`
3. `state_slot`

Where:

- `path_a` and `path_b` are pointers into `.rdata`
- `state_slot` is a pointer into zero-initialized `.data`

The `state_slot` values are reused across the two tables, which strongly
suggests:

- each slot is a global resource-state object
- the tables map one logical resource to one runtime slot
- different tables provide alternate path pairs for the same slot

## Table A: `0x6A2C20`

This table leans toward uppercase and archive-style paths.

Representative rows:

| Slot | Path A | Path B |
|---|---|---|
| `0x759744` | `GLOBAL/GMODELS.mdl` | `/global/pak/GMODELS.mdl` |
| `0x7597F4` | `GLOBAL/GMODELS.tex` | `/global/pak/GMODELS.tex` |
| `0x7596B4` | `SPLINES.SPL` | `splines.SPL` |
| `0x759564` | `GLOBAL/WEATHER.INI` | `levels/global/WEATHER.INI` |
| `0x759734` | `GLOBAL/INITSCR/_ENG.TXT` | `initscripts/frontend/languages/_eng.txt` |
| `0x75976C` | `GLOBAL/INITSCR/_KEY1.TXT` | `initscripts/frontend/_key1.txt` |
| `0x75978C` | `GLOBAL/INITSCR/_KEY2.TXT` | `initscripts/frontend/_key2.txt` |
| `0x7597E4` | `GLOBAL/INITSCR/_KEY3.TXT` | `initscripts/frontend/_key3.txt` |

Interpretation:

- this table looks like a canonical or archive-oriented path layer
- many rows pair an older uppercase path with a lower-case frontend or packed
  path

## Table B: `0x6A2F20`

This table leans toward lower-case PC-era startup assets and localized text.

Representative rows:

| Slot | Path A | Path B |
|---|---|---|
| `0x759744` | `global/gmodelspc.mdl` | `global/gmodelspc.mdl` |
| `0x7597F4` | `global/gmodelspc.tex` | `global/gmodelspc.tex` |
| `0x75958C` | `mat_pc.bin` | `mat_pc.bin` |
| `0x759704` | `resource1.glg` | `GLOBAL/levelSetup.ini` |
| `0x7595AC` | `GAME.GXT` | `GAME.GXT` |
| `0x7595DC` | `GAME_GER.GXT` | `GAME_GER.GXT` |
| `0x7597DC` | `GAME_FRE.GXT` | `GAME_FRE.GXT` |
| `0x759674` | `GAME_ITA.GXT` | `GAME_ITA.GXT` |
| `0x759754` | `GAME_SPA.GXT` | `GAME_SPA.GXT` |
| `0x75973C` | `GAME_RUS.GXT` | `GAME_RUS.GXT` |
| `0x7596E4` | `global/initscripts/_charJap.txt` | `global/initscripts/_charJap.txt` |

Interpretation:

- this table looks much closer to the real PC startup content set
- it includes the exact `GAME.GXT` and `gmodelspc.*` resources relevant to the
  observed post-window bootstrap
- localized text packs are explicitly table-driven, not discovered dynamically

## State Slot Evidence

The repeated `state_slot` values are not dead data.

Examples:

- `0x759744`
- `0x7597F4`
- `0x759704`
- `0x7595AC`
- `0x7595DC`

These slots have code references in grouped stub blocks such as:

- `0x633C20`, `0x633C40`, `0x634180`, `0x6341A0`, `0x6341C0`
- `0x6362B0`, `0x6362C0`, `0x636560`, `0x636570`, `0x636580`

And startup/runtime code also reads some of them directly, for example:

- `0x7D2584` reads `0x759744`
- `0x7D25A7` reads `0x7596B4`

This is why the current best interpretation is:

- one slot == one global resource-state object
- one or more descriptor tables point at the same slot with different path
  variants

## What This Says About Startup Design

This is another concrete part of the Manhunt 2 startup:

- core startup resources are table-driven
- path variants are pre-authored in static data
- localized text packs are enumerated explicitly
- runtime work can be reduced to:
  - choose row
  - pick path variant
  - load into the row's state slot

That is much cheaper and more predictable than scanning directories or
discovering assets dynamically on boot.
