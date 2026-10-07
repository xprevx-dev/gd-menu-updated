# v2.5.0
The "make it infinitely better" update.

## Safety & stability
- **Crash-proof replay loading.** A corrupt or deliberately forged `.gdr2` / `.gdbot` file could crash Geometry Dash (the replay library reserved memory using counts read straight from the file, and its reader stalls instead of failing on truncated data). Every import now goes through a validator that rejects impossible files, so a bad download shows as "unsupported" instead of killing the game. Covered by 49 host-side unit tests incl. a fuzzer, run in CI on every push.
- **Crash-proof sessions.** Same hardening for resume-session files.
- The two `destroyPlayer` hooks (noclip + safe mode/accuracy) were merged into one: they ran in an order Geode doesn't promise, which could silently break noclip accuracy counting - or let a noclip player die.
- Saving a bot named `CON`, `NUL`, `COM1`... no longer silently fails on Windows; names are also length-capped.
- Null-checks in record/playback paths; `Resuming` now handled on level complete; stopping playback releases held buttons properly.

## Keybinds
- **Real keybinds.** The seven PC keys are now native Geode keybind settings: capture any key, modifier combo (`Ctrl+Shift+...`) or mouse button in Settings, with Geode's own capture UI. The old single-letter strings are gone.
- Keybind and setting edits apply **instantly** (no more re-entering the level).
- Two actions on the same combo: the second is disabled and the Keys tab tells you about it.

## New features
- **In-game HUD** (Settings, off by default): bot state, frame, percent, speed, a **live input viewer** (which buttons you or the bot are holding, per player) and noclip accuracy in a corner of your choice. GDMenu stays invisible in gameplay unless you ask for it.
- **Player Trail** (Hacks tab): draws your flight path in the theme colour - study bot lines and wave corridors.
- **Loop Playback**: the bot auto-restarts when it dies, so you can watch a run on repeat.
- **Stop Playback At %**: playback bails automatically at a percent - drill one section.
- **Auto-Save Bot On Complete**: finishing a run while recording also saves a dated `.gdr2`.
- **Sessions manager** (Bot tab): every saved resume point for every level, with per-session delete and clear-all. Sessions no longer pile up forever.
- **Bots library upgrade**: search box (name or level), sort by name/newest/inputs, rename button, file size + "phys" badge per row, and parsed file info is cached so opening the tab stays fast with big libraries.
- **Custom theme colour**: an 8th theme slot driven by an RGB picker in Settings.
- **Lifetime stats** in the More tab: recordings, saves, resumes, plays, corrupt files blocked.
- One-time **intro popup** on first launch.

## Looks
- Tabs slide+fade in; active toggle rows get an accent strip in your theme colour; the bubble pulses while recording.

## For developers
- All byte-level serialization moved to `src/core/replay_io.hpp` (Geode-free) with `tests/` unit + fuzz tests (`bash tests/run_tests.sh`, also in CI).
- New tag-triggered release workflow publishes `.geode` builds to GitHub Releases.

# v2.4.1
- **Autoclicker** (More tab): 1-60 clicks/sec, gets recorded by the bot like real clicks
- **Safe Mode** (on by default): no new best % or completion is saved after using noclip, speedhack, autoclicker, frame stepper, start pos or bot playback during an attempt
- **Noclip Accuracy**: optional small % + deaths counter while noclip is on (off by default)
- **Menu Themes** (Style tab): 7 accent colours, bubble opacity and size
- **Preset Profiles** (Style tab): 3 slots (Practice / Showcase / Custom) that save and load your hack settings

# v2.4.0
- Per-tick physics fix: playback follows the exact recorded path (practice-mode bots no longer drift)
- Bubble is now a true circle and appears on every screen (search, level info, creator, settings...) except gameplay

# v2.3.0
- Floating GDM bubble now shows on every screen except gameplay
- Bot saves each input's position/speed (GDR2 "Phys" extension) and uses it on playback, which fixes practice-mode bots dying
- Rewrote about page

# v2.2.0
- Fixed phantom jumps from left/right keys in normal levels (broke Eclipse playback)
- Exact practice-mode respawn and held-button sync

# v2.1.0
- Eclipse-compatible timing, auto-pause Click Between Frames, practice fix, copy to Eclipse

# v2.0.0
- New tabbed menu, bots library, .gdr2 / .gdbot saving
