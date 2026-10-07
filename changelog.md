# v2.6.0
The Mega Hack / Eclipse feature drop + always-on attempt replay.

## Always-on attempt clips - "show me that run again"
- **GDMenu now records every attempt, all the time** (new **Clips** tab). A clip stores *input events only* - nothing runs per tick, so recording costs no performance and never touches your physics.
- **Watch** replays any of your last attempts live through the bot engine - your run, reproduced on screen. **Save** exports it as a standard `.gdr2` (+ Eclipse copy), **X** deletes it.
- Keeps the last **N attempts** (default 5, up to 50, 0 = off - adjustable right in the Clips tab). The ring is also memory-capped (32 MB) and evicts oldest-first; its logic is Geode-free and covered by **41 new host-side unit tests** (CI, incl. ASan/UBSan).
- Clips know their context: attempt number, % reached, duration, input count, practice mode, a green **COMPLETE** badge - and a **CBS/CBF!** flag when sub-tick inputs were possible (those can drift <1 tick on replay; no tick-based bot can do better).
- Practice clips survive checkpoint respawns (inputs after the respawn point are trimmed, same rule as bot recording); quitting mid-attempt keeps the clip.

## Bot system: CBS/CBF compatibility fixes
- **CBS (Click Between Steps - RobTop's official 2.208 feature)** is now paused while the bot records/plays/resumes and restored afterwards: tick-based replays can't express half-step inputs. Same policy Eclipse uses for CBF. Toggle: **Pause CBS/CBF For Bot** (on by default).
- **CBF integration hardened**: GDMenu only touches Syzzi's `soft-toggle` when that setting actually exists in the installed CBF version - a renamed/missing key can no longer silently no-op or clobber a wrong value.
- **Pause CBS For Clips** (new, off by default): forces vanilla CBS off while passive clips record, for frame-perfect clip replays. Off by default = your vanilla CBS choice is never touched while you just play.

## New hacks (Mega Hack / Eclipse style)
- **Noclip per player**: protect P1 and/or P2 separately (dual & 2-player practice).
- **Noclip limits**: *hit limit* (after N saves, hits are lethal again) and *accuracy floor* (below X% noclip accuracy, hits are lethal) - one notification when it trips, resets per attempt.
- **Quick Respawn**: auto-restart 0.1-3.0 s after dying (Mega Hack's "Respawn Time"). Practice keeps vanilla checkpoint behaviour; the frame stepper still freezes everything.
- **Sync Music With Speedhack**: pitches the song with your speedhack (Eclipse / xdBot style), auto-restores when it ends or you leave the level.
- **Force Hitboxes On Death**: keeps GD's show-hitboxes-on-death forced on while enabled.
- **Cheat Indicator**: the GDM bubble says **CHEATS** in red while any hack is active - Safe Mode still blocks saving cheated progress.
- **HUD counters**: FPS (wall-clock measured, speedhack-proof), attempts, jumps and level time on one compact line - each optional; the HUD itself stays off by default.

## Menu
- New **Clips** tab (sidebar tightened to fit 8 tabs); the Hacks tab scrolls with all the new rows.

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
