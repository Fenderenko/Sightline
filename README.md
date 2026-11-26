# Sightline

**English** · [Українська](README.uk.md)

A custom crosshair overlay for games running in borderless / windowed fullscreen on Windows 10/11.
The crosshair is drawn in its own transparent window: nothing is injected into the game. No hooks, no drivers, no memory reading.

## Download

Get the latest build from [Releases](../../releases): unzip it and run `sightline.exe`.
The crosshair appears immediately; the tray icon opens the settings.

> The executables are not code-signed, so Windows SmartScreen may show a warning:
> "More info" → "Run anyway".
> To avoid it, right-click the downloaded zip → Properties → tick "Unblock" → OK, then unzip.
> Updates installed from Settings → About never show this warning.

## Features

- **Library of 59 presets.** The most used styles come first: compact static crosses with and without outline, in various thicknesses and lengths, and dots. Then circles, sniper, shapes, accessibility and fun. Search (Ctrl+F), categories and favourites.
- **Editor:**
  - 13 shapes: cross, plus, X, T, dot, ring, box, diamond, triangle, corners, chevron, segments, line.
  - Layers, outline, rotation, offset, separate vertical length, "centre dot".
  - Fill and outline blur (soft crosshair, soft shadow / glow).
  - Colour picker with hex entry, compact value fields (drag, type or mouse wheel).
  - Canvas rulers with pixel offsets from the centre; copy / paste layers and layer styles.
  - Undo / redo (Ctrl+Z / Ctrl+Y).
  - Explicit saving: "Save" / "Save as new" / "Discard changes". Built-in presets are never modified: saving creates your own copy under "Mine".
- **Pixel-exact.** All elements share one centre, so the crosshair is always symmetric.
- **Code import:** Sightline `SL1.…`, Valorant (`0;P;…`) and CS2 (`CSGO-…` and `CS…`, rescaled to your monitor). Library export and import as JSON.
- **Game profiles** by exe (own crosshair and offset), process filter, Competitive-safe list. Games are picked by browsing to the exe or from a list of running apps.
- **Aim down sights (ADS):** hide the crosshair, show it only while aiming, or switch to another one.
- **When to hide the crosshair:** on the desktop, while the mouse cursor is visible (menus, inventory), or outside fullscreen games.
- **Other:** monitor selection, monitor or game-window centre, sleep until requested, exclusion from streams and recordings.
- **Look:** dark theme by default, light or follow the system.
- **Languages:** English (default), Ukrainian, German, French, Spanish, Portuguese (Brazil), Polish, Italian, Turkish.
- **Updates:** Settings → About checks GitHub on request and installs the new release in place. Nothing is checked on its own.

## Default hotkeys

| Action | Keys |
|---|---|
| Show / hide crosshair | Ctrl+Shift+F9 |
| Next / previous crosshair | Ctrl+Shift+F8 / F7 |
| Settings | Ctrl+Shift+F11 |
| Specific crosshair | per-crosshair hotkey |
| Nudge crosshair by 1 px | modifier + arrow keys (off by default) |
| Aim down sights (ADS) | unbound (RMB, Mouse 4/5 or a key) |

## Resources (static crosshair, Windows 11, 2560×1440)

| Core mode | CPU | Memory (private) |
|---|---|---|
| Static crosshair | 0 | ~2.5 MB |
| + ADS key bound (125 Hz polling) | ~0.2 % of one core | ~2.5 MB |
| + hide while cursor is visible (10 Hz) | below measurement precision | ~2.5 MB |

Redrawing the crosshair takes 0.04–2.6 ms and happens only when it changes, never per frame. The core loads no graphics libraries at all.
The settings window is a separate process that exits completely when closed.

## Safety and anti-cheat

Sightline does not inject DLLs, hook DirectX/OpenGL/Vulkan, read or modify game memory or files, analyse screen pixels, send input or install drivers.
It goes online only when you press "Check for updates" (api.github.com and the release download).
Still, no third-party overlay has an official guarantee from game developers, and some tournament platforms forbid third-party crosshairs. Check the game's rules; for such games there is the Competitive-safe list.

## Building from source

Requires Visual Studio 2022 (or Build Tools) with "Desktop development with C++" and Windows SDK 10.0.22621+.

```powershell
.\build.cmd            # release → bin\sightline.exe, bin\sightline-settings.exe
.\build.cmd debug
$env:SL_TEST = "1"; .\build.cmd   # also builds and runs the self-test
```

- `sightline.exe` — core (overlay, hotkeys, tray). Launching it again opens the settings; `sightline.exe --quit` stops the core.
- `sightline-settings.exe` — settings.
- Config: `%APPDATA%\Sightline\config.json` (+ `.bak`). Logs: `%LOCALAPPDATA%\Sightline\logs`.

Releases are built by GitHub Actions: every push to `main` whose version (in `res/app.rc`) has no release yet builds, self-tests and publishes `v<version>` with notes from `docs/releases/`.

## Layout

```
src/common    model, JSON, CPU crosshair rasteriser, Valorant/CS2 codes, logo, i18n, IPC
src/core      sightline.exe: overlay, hotkeys, profiles, tray
src/settings  sightline-settings.exe: Direct2D UI
tools/        icon generation at build time
tests/        self-test for rendering, symmetry and config
docs/         architecture, risks, design mock-ups, release notes
```

More: [docs/architecture.md](docs/architecture.md).
