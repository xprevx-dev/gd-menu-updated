# v2.7.0
The biggest feature drop yet: 18 new settings, a searchable Hacks tab and a new hook layer (`gamehacks.cpp`).

## New hacks
- **Search bar in the Hacks tab** - filters every row by name or description. The whole tab is now generated from one row table, and clicks update rows in place: scroll position and search focus survive.
- **All Passable (experimental)** - fall straight through blocks and solids; hazards still kill (pair with noclip for full ghost mode).
- **Jump Hack (infinite jumps)** - hold jump mid-air to keep re-jumping and hover upward.
- **Physics Bypass (experimental)** - physics runs at a fixed 240 ticks/s regardless of your monitor FPS (CBF/xdBot style). Composes with speedhack; below ~15 render FPS the game slows down instead of skipping (spiral guard).
- **Free Attempts** - the on-screen attempt counter stays at 1 (cosmetic; your real stats are untouched).
- **Force Platformer (experimental)** - play ANY level in platformer mode; the shared level object is restored when you quit.
- **Auto Practice Mode** - practice mode turns itself on when a level starts.
- **Practice Music Bypass** - checkpoint respawns stop restarting/resyncing the song.
- **Hide Pause Menu** - pause closes itself after 0.8 s (anti-rest); it never closes while the GDMenu panel is open.
- **Audio Pitch Shift** (0.25x-4x) - pitch the music independently of game speed; combines with Sync Music With Speedhack.

## Visual cleanup
- **Layout Mode (Editor View)** - enter any level and see its flat editor-style layout instead of the decorated render: white solids, red hazards, purple portals, yellow pads, cyan rings/orbs, gold coins, faint grey decorations. Built on a load-time X-sorted object list + per-frame camera-rect query (zoom/mirror/platformer aware), so it costs nothing off-screen and never touches physics. Mid-level trigger-spawned objects stay decorated; a restart restores everything.
- **No Particles** (level-spawned particle objects; death effects stay), **No Pulse** (pulse triggers leave colours untouched), **No Wave Trail**.
- **Unlock Icons** - every icon looks unlocked in the garage (client-side cosmetic; server-side items untouched).
- **Player Trail now rolls**: new **Trail Length** setting (1-60 s, default 20). The trail rotates through chunk draw-nodes instead of wiping at a hard segment cap - steady length, no flicker, no redraw cost.

## HUD (still optional, still off by default)
- **Best Run** (session best % on the current level), **CPS** (jump clicks per wall-clock second), **Run From** (the % your current run started at - covers start pos and practice checkpoints), **Percent Decimals** (1-3).
- **Noclip accuracy fixed**: stats no longer count CBS half-ticks, which used to dilute accuracy towards 100% whenever Click Between Steps was on.

## Safety
- The new gameplay-affecting hacks (All Passable, Jump Hack, Physics Bypass, Force Platformer) all feed **Safe Mode** and the red **Cheat Indicator**, exactly like the old ones.

## Known limits (honest list)
- No Particles hides *level* particle objects only - the death explosion is a different system.
- No Pulse passes colours through untouched; HSV-mode pulses that animate outside the colour callback may still move.
- Copy Hack, No Short Numbers, custom wave-trail colour, portal lighting / mirror toggles, auto song download and show-trajectory have no clean hook point in the 2.2081 bindings - skipped rather than half-broken.

## Platform
- **iOS verified end-to-end**: every hook this update adds (no particles, no pulse, all passable, jump hack, unlock icons, force platformer, hide pause, practice music bypass, pitch shift...) binds to a real iOS address in the GD 2.2081 bindings, the only Windows-specific code path has a POSIX fallback, and CI builds the iOS target on every push and every release. The release `.geode` is one **combined** package: the same file works on Windows, macOS, iOS and Android (Geode 5, GD 2.2081).

# v2.6.0
The Mega Hack / Eclipse feature drop + always-on attempt replay.

## Always-on attempt clips - "show me that run again"
- **GDMenu now records every attempt, all the time** (new **Clips** tab). A clip stores *input events only* - nothing runs per tick, so recording costs no performance and never touches your physics.
- **Watch** replays any of your last attempts live through the bot engine - your run, reproduced on screen. **Save** exports it as a standard `.gdr2` (+ Eclipse copy), **X** deletes it.
- **Save Attempt** (Bot tab): exports the attempt you're in *right now* - works mid-run from the pause menu ("save my 63% so far"), falls back to your most recent finished attempt.
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
