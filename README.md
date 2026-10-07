# GDMenu
Geode mod for GD 2.2081 (Geode v5): a replay bot (GDR2 / .gdbot), resume where you left off, frame stepper, noclip, speedhack, hitboxes, start-pos switcher and an optional in-game HUD. Works on **PC and mobile**.

<img src="logo.png" width="150" alt="the mod's logo" />

## How to use
Nothing is shown while you play (unless you enable the HUD). **Pause** and tap the floating **GDM bubble** - you can drag it anywhere and it remembers where you put it. It also floats on every other screen (main menu, level lists, search...).

| Tab | What's inside |
|---|---|
| **Bot** | Status, Record / Play / Save Bot, **Resume session**, and the **Sessions** manager (every saved resume point, per-level delete, clear all) |
| **Bots** | Your replay library: search, sort (name / newest / inputs), Load / Rename / Delete, Open Folder. Shows inputs, length, level, size and whether a file carries physics data |
| **Hacks** | Noclip, Show Hitboxes, Speedhack with fine speed controls |
| **Tools** | Frame Stepper (+ touch step buttons on phones), Start Pos Switcher, Loop Playback, Stop-At-% |
| **More** | Autoclicker, Safe Mode, Noclip Accuracy |
| **Style** | 7 theme accents, bubble opacity/size, 3 preset profiles |
| **Keys** | Your keybinds at a glance + conflict warnings; click one in Settings to capture a new key |

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

## Safe Mode
On by default: after noclip / speedhack / autoclicker / stepper / start pos / bot playback was used in an attempt, GDMenu makes sure **no percent or completion is saved or submitted**. Keep it on - it's what makes this a practice tool instead of a cheat.

## Mobile & PC
- **PC:** keybinds for everything, hidden during gameplay.
- **Mobile:** bigger buttons, plus **+1 / +10 / Play** touch buttons that appear only while the frame stepper is on.

## Build & test
Pushes and tags are built automatically by GitHub Actions; tags (`v*`) also publish a GitHub Release (download the `.geode` from Releases). To build locally: `geode build`.

The serialization core is plain C++ and has its own tests (no Geode or GD needed):
```
bash tests/run_tests.sh
```

## Credits
Started by **Cyber39DreamGD**; v2.5.0 and later maintained by **Prevx**. Built on [Geode](https://geode-sdk.org) and [GDReplayFormat](https://github.com/maxnut/GDReplayFormat).
