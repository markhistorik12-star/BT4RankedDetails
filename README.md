# BT4 Ranked Details

Read-only online latency / rollback HUD overlay and offline match-history viewer for **DBZ BT4 Tag Team Decomp (`BT4decomp`)** — built completely **without Steam** (compatible with BT4decomp's native P2P/WebRTC rollback netcode and Cloudflare Workers / Supabase Ranked backend).

## Features

- **In-Game Overlay (Zero Vulkan/DX12 Hooks):**
  - Click-through translucent rounded Win32 layered overlay anchored to `BT4decomp.exe`.
  - Live **Ping (RTT ms)** colour-coded by signal grade, **10 s Jitter (±ms)**, **Direct (`DIR`) vs TURN Relay (`RLY`)** badge, **Rollbacks/s & depth**, **Waits (stalls)**, **Input Delay (frames)** + **Delay Swing bar**, and **Desync alert**.
  - Hot-reloaded configuration via `ranked_details\BT4RankedDetails.ini` (changes apply within 1 s).
- **Native Lua Companion Mod (`mods\bt4_ranked_details\mod.lua`):**
  - Draws a live **Time-Out tug-of-war bar** under the timer in Team Battle / Tag Team.
  - Emits compact read-only combat telemetry (`BT4RD|...`) for damage, healing, combos, Max Power, counters, switches and HP timelines.
- **Zero Modification of `BT4decomp.exe`:**
  - Loaded via transparent `winmm.dll` proxy forwarding to `System32\winmm.dll`.
  - Keeps `BT4decomp.exe` and its `gameplay fingerprint` 100% untouched for official Ranked & Quick Match compatibility.
  - Uses AOB pattern scanning with RIP-relative decoding + SEH fail-closed reads for netcode stats, and a read-only `WINHTTP.dll` IAT observer for `bt4_ranked_api` points/streaks/opponent metadata.
- **Offline Match History Viewer (`ranked_details\history\index.html`):**
  - Double-click `index.html` straight from `file://` (no server, no internet, no Steam).
  - Per-match pages, opponent grouping by BT4 account ID, combat comparison, HP/Ping/Delay/Rollback/FPS timelines, incident log (desyncs, disconnects, forfeits), ES/EN language switch, light/dark themes, and 1080×1080 shareable PNG cards.

## Build from Source (Windows x64)

Requires Visual Studio 2019/2022 Build Tools (C++ x64):

```bat
build.bat
```

Outputs `build\winmm.dll` and `build\test_telemetry.exe`.

## Install into `BT4decomp`

```bat
install.bat "G:\Jueguitos\BT4decomp-windows-x64\BT4decomp"
```

1. Launch `BT4decomp.exe`.
2. (Optional, recommended for combat stats & Time-Out bar) Press `F1` > **Mods** > enable **BT4 Ranked Details**.
3. After an online match, open `BT4decomp\ranked_details\history\index.html`.

To uninstall, run `uninstall.bat` (or delete `winmm.dll`). Your match history in `ranked_details\data\` is preserved.

## Architecture

- `src/winmm_proxy.cpp` & `src/winmm.def` — Transparent `winmm.dll` proxy & worker thread.
- `src/aob_scanner.hpp` — Dynamic AOB scanner + RIP-relative offset decoder + SEH fail-closed readers for `bt3_net_get_meter_stats`, `net_is_relay`, `net_is_online`, `net_local_side`, `fps` and `speed_pct`.
- `src/winhttp_iat.hpp` — Read-only `WINHTTP.dll` IAT observer for `bt4_ranked_api` & matchmaking JSON responses.
- `src/telemetry.hpp` — Session aggregator, `bt3_log.txt` + `BT4RD|` bridge, 10 s jitter calculator, incident logger, and atomic file writer (`state.json`, `matches.jsonl`, `matches.log`, `incidents.jsonl`, `history/data.js`, `history/timelines/<id>.js`).
- `src/overlay.hpp` — GDI+ click-through layered HUD overlay.
- `src/config.hpp` — Hot-reloading INI parser.
- `mod_lua/bt4_ranked_details/mod.lua` — Native BT4decomp Lua companion mod.
- `history/` — Offline Svelte 5 + D3 match-history SPA adapted for BT4 (no Steam).

## Credits & Licenses

Inspired by and viewer-adapted from **Sparking Ranked Details** (Svelte 5 MIT, D3 ISC). Designed for **DBZ BT4 Tag Team Decomp** (`NauTaro/DBZ-BT4-Tag-Team-Decomp`, upstream `z3xox/Tenkaichi3Decomp`).
