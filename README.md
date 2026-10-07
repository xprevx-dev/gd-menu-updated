# GDMenu
Geode mod for GD 2.2081 (Geode v5): a replay bot (GDR2 / .gdbot), **always-on attempt clips** (watch/save your last runs), resume where you left off, frame stepper, a **searchable hacks tab** (noclip + limits, speedhack, pitch shift, physics bypass, all passable, jump hack, force platformer, free attempts, auto practice, no particles/pulse/wave trail, unlock icons...), hitboxes, start-pos switcher, quick respawn and an optional in-game HUD (FPS, CPS, best run, run-from). Works on **PC and mobile**.

<img src="logo.png" width="150" alt="the mod's logo" />

## How to use
Nothing is shown while you play (unless you enable the HUD). **Pause** and tap the floating **GDM bubble** - you can drag it anywhere and it remembers where you put it. It also floats on every other screen (main menu, level lists, search...).

| Tab | What's inside |
|---|---|
| **Bot** | Status, Record / Play / Save Bot, **Save Attempt** (the run you're in right now → `.gdr2`, even mid-attempt), **Resume session**, and the **Sessions** manager (every saved resume point, per-level delete, clear all) |
| **Bots** | Your replay library: search, sort (name / newest / inputs), Load / Rename / Delete, Open Folder. Shows inputs, length, level, size and whether a file carries physics data |
| **Clips** | **Always-on attempt recorder**: your last N attempts with % / time / inputs - **Watch** one replay live, **Save** it as `.gdr2`, or delete it |
| **Hacks** | **Searchable list of every hack**: Noclip (+ per-player, hit & accuracy limits), All Passable, Jump Hack, Physics Bypass, Free Attempts, Speedhack (+ speed & pitch steppers, music sync), Quick Respawn, Auto Practice, Force Platformer, Practice Music Bypass, Show Hitboxes (+ on death), No Particles / Pulse / Wave Trail, Player Trail (+ length), Unlock Icons, Hide Pause, CBS/CBF compatibility, Cheat Indicator, Safe Mode |
| **Tools** | Frame Stepper (+ touch step buttons on phones), Start Pos Switcher, Loop Playback, Stop-At-% |
| **More** | Autoclicker, Safe Mode, Noclip Accuracy, lifetime stats |
| **Style** | 8 theme accents (last one is your own RGB colour), bubble opacity/size, 3 preset profiles |
| **Keys** | Your keybinds at a glance + conflict warnings; click one in Settings to capture a new key |

Also in Settings: **In-Game HUD** options (state / frame / percent / speed / live input viewer), **Auto-Save Bot On Complete**, and the HUD corner & size. First launch shows a one-time intro popup.

## Keybinds (PC)
Every action has a **native Geode keybind** (Settings > GDMenu): capture any key, modifier combo like `Ctrl+Shift+G`, or a mouse button. Defaults: `F` stepper, `G` step, `N` noclip, `H` hitboxes, `S` speedhack, `Q`/`E` start pos. Edits apply instantly, and if two actions share a combo the second one is disabled and the Keys tab says so.

## Bot files
- `.gdbot` uses **exactly the same binary layout as `.gdr2`** ([GDReplayFormat v2](https://github.com/maxnut/GDReplayFormat)) with the standard "Phys" extension, so your bots also work in Eclipse Menu, xdBot and other GDR2 bots. One tap copies them into Eclipse's replay folder.
- Frames use `m_currentProgress` (240 ticks per second); player-2 inputs are only saved in 2-player levels (GDR2 convention).
- Quit while recording and the session is saved: **Bot > Resume** fast-forwards to your exact frame, freezes there and keeps recording.
- Corrupt or forged replay files can't crash the game any more - they show up as unsupported. (There are unit + fuzz tests for this in `tests/`, run in CI on every push.)

## Playback options
- **Loop Playback** - the bot restarts automatically when it dies, for watching runs on repeat.
- **Stop Playback At %** - playback stops at a percent so you can drill one section.
- **Quick Respawn** - your own attempts auto-restart 0.1-3.0 s after death (Mega Hack style, 0 = off).

## Attempt clips ("watch that run again")
GDMenu records **every attempt all the time** - inputs only, no per-tick work, so it costs no performance. Pause > GDM bubble > **Clips**:

- **Watch** - the bot engine replays the attempt live on screen (your run, reproduced).
- **Save** - exports it as a standard `.gdr2` into your library (+ Eclipse's folder when installed).
- Each row shows attempt number, % reached, duration, input count, `practice` / `COMPLETE` / `CBS/CBF!` badges.
- Keeps the last N attempts (default 5, max 50, 0 = off) with a 32 MB memory cap, oldest evicted first.
- **CBS** (vanilla Click Between Steps) and **CBF** (Syzzi's Click Between Frames) land inputs *between* ticks, which no tick-based replay can reproduce: GDMenu pauses both while the bot runs (restores them after), flags clips recorded with them, and can optionally pause CBS for clips too.

## Safe Mode
On by default: after noclip / speedhack / autoclicker / stepper / start pos / bot playback / all passable / jump hack / physics bypass / force platformer was used in an attempt, GDMenu makes sure **no percent or completion is saved or submitted**. Keep it on - it's what makes this a practice tool instead of a cheat. The **Cheat Indicator** backs it up visibly: while any hack is active, the floating GDM bubble says **CHEATS** in red (Mega Hack style). Safe Mode itself is a row in the Hacks tab, so turning it off is a deliberate, searchable action.

## Mobile & PC
- **PC:** keybinds for everything, hidden during gameplay.
- **Mobile:** bigger buttons, plus **+1 / +10 / Play** touch buttons that appear only while the frame stepper is on.

## Build & test
Pushes and tags are built automatically by GitHub Actions; tags (`v*`) also publish a GitHub Release (download the `.geode` from Releases). To build locally: `geode build`.

The serialization core and the clip ring are plain C++ with their own tests (no Geode or GD needed):
```
bash tests/run_tests.sh          # 53 replay/session checks + 41 clip-ring checks
SANITIZE=1 bash tests/run_tests.sh   # same under ASan + UBSan (what CI runs)
```

## Credits
Started by **Cyber39DreamGD**; v2.5.0 and later maintained by **Prevx**. Built on [Geode](https://geode-sdk.org) and [GDReplayFormat](https://github.com/maxnut/GDReplayFormat).
