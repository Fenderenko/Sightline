# Sightline — architecture (v0.1)

## Processes

```
┌──────────────────────────────┐        WM_COPYDATA (JSON)        ┌──────────────────────────────┐
│ sightline.exe  (Core)        │ <──────────────────────────────> │ sightline-settings.exe (UI)  │
│ · overlay window (DComp)     │   1 apply config / 2,3 hotkeys    │ · Direct2D immediate-mode UI │
│ · hotkeys (RegisterHotKey)   │   10 config changed by Core       │ · exits when window closes   │
│ · ADS poll (only when bound) │                                   │ · writes config.json         │
│ · WinEventHook (foreground)  │                                   └──────────────────────────────┘
│ · tray icon, profiles        │
└──────────────────────────────┘
        │ never touches the game process except
        │ OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) → exe name
        ▼
   Game window (borderless / windowed)
```

`sightline-telemetry.exe` (ETW frametime + sensors) arrives in v0.2.

## Threads

Core is single-threaded: one `MsgWaitForMultipleObjectsEx` loop.
- No periodic timers while the crosshair is static.
- A high-resolution waitable timer (125 Hz) runs only while an ADS key is bound and the overlay can be visible.
- One-shot timers: re-topmost and re-center 500 ms / 2 s after a foreground change; debounced config save.

## Overlay window

- `WS_POPUP` + `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED` (click-through, never activated).
- Pixels come from the shared CPU rasteriser (`src/common/crosshair_render.cpp`, 4×4 supersampling, integer rects for axis-aligned shapes), which also feeds the settings previews. What you see in the editor is exactly what the game sees.
- **Layered backend (default):** pixels go into a DIB and `UpdateLayeredWindow`. No D3D, D2D or WIC is loaded (graphics DLLs are delay-loaded). Measured: 2.5 MB private, 0 ms CPU over 20 s.
- A DirectComposition (GPU) backend existed in v0.1 and was removed: identical pixels and CPU, but +~55 MB private memory and 24 threads.
- The surface is the crosshair bounding box only (odd size, typically 15–61 px), never full screen.
- The overlay redraws only when the crosshair changes. Software Direct2D was rejected because it keeps a WARP device alive (~55 MB).

## Pixel model

The crosshair center is an integer pixel index. Axis-aligned shapes are built from integer rectangles and drawn aliased, so 1 px lines stay sharp. Rotated or round shapes use anti-aliased geometry around the pixel center.

## Polling (the only periodic work in Core)

One waitable timer, armed only when needed:
- 8 ms (~125 Hz) while an ADS key is bound and its action is not "none";
- otherwise 100 ms (10 Hz) while "hide while the cursor is visible" is on (`GetCursorInfo`);
- otherwise cancelled: zero wake-ups.

## Crosshair codes

`src/common/codes.cpp`:
- Valorant: primary section (`P`) keys `c/u` colour, `h/t/o` outline, `d/z/a` centre dot, `0*`/`1*` inner/outer lines (`b` show, `t` thickness, `l`/`v` length, `o` offset, `a` opacity).
- CS2: base-57 share codes with a checksum. Legacy `CSGO-` (v1 tenths scaled by YRES; v3/v4 pixels with a stored screen height) and the current `CS` + 44 chars (pixels, signed gap, outline colour). Pixel values are rescaled from the stored screen height to the primary monitor.

## Config

`%APPDATA%\Sightline\config.json`: atomic write (`.tmp` → replace) with `.bak` fallback. Built-in presets live in code. A modified preset is stored under its own id and overrides the built-in one; "Reset" deletes the override.

## Deviations from the original prompt (with reasons)

| Prompt | v0.1 | Why |
|---|---|---|
| CMake + vcpkg | `build.cmd` (cl/link/rc) | CMake and vcpkg are not installed on the build machine; no downloads needed |
| Dear ImGui settings | custom Direct2D immediate-mode UI | zero dependencies; exact match of the reference visual (grain, hairlines, gradients) |
| Named pipe IPC | `WM_COPYDATA` | both sides are GUI processes with message loops; event-driven, no extra thread |
| Raw Input for ADS | `GetAsyncKeyState` poll at 125 Hz, only while bound | Raw mouse input delivers every movement (1–8 kHz) and breaks the CPU budget |

## Risks

1. **Independent Flip.** Any topmost window over a borderless game may force DWM composition on hardware without free MPO planes. Mitigation: tiny surface, present-on-change. A PresentMon-based check arrives with the telemetry helper (v0.2).
2. **Games that re-assert topmost.** Re-topmost is event-driven (foreground change + 2 delayed checks). The UIAccess band is planned for v1.1.
3. **Elevated games (UIPI).** `GetAsyncKeyState` and hotkeys may not see input while an elevated game is in front. Workaround: run Core as administrator.
4. **Exclusive fullscreen.** Not supported by design (no injection). The UI says so.
5. **Anti-cheat policy.** No vendor guarantees. The Safety page states this, and the Competitive-safe list disables the overlay per exe.
