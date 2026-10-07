# GDMenu architecture

How the mod is put together, where things live, and the rules we follow when changing it.

## File map

| File | Responsibility |
|---|---|
| `src/core/replay_io.hpp` | **All byte-level I/O.** Session files, per-tick fix files, GDR2 replay types, file-name sanitising, and the corrupt-file validator. Geode-free on purpose: it compiles and tests on a desktop compiler (`tests/`). |
| `src/state.hpp` | Shared POD state (`BotData`, `HackData`), the `bot::` / `hacks::` / `extras::` / `replays::` / `practice::` / `hud::` API surface. No logic. |
| `src/bot.cpp` | Recording / playback / resume engine + GDR2 file library + all `PlayLayer`/`GJBaseGameLayer` hooks that drive the bot. |
| `src/hacks.cpp` | Noclip, speedhack, hitboxes, frame stepper, start-pos switcher, keybind press handling, settings reload. |
| `src/extras.cpp` | Autoclicker, Safe Mode, noclip accuracy, themes, profiles, stats counters, and the **single** `destroyPlayer` hook. |
| `src/practice.cpp` | Checkpoint physics snapshot/restore so practice-mode recordings don't desync. |
| `src/hud.cpp` | Optional in-game overlay (off by default). |
| `src/trail.cpp` | Optional flight-path drawing. |
| `src/ui.cpp` | Floating bubble, tabbed panel, all popups (save/rename/sessions/intro). |

## Hook map (who modifies what)

Multiple `$modify` classes target the same GD classes. Order between *different functions* doesn't matter; order between two hooks of the *same function* is **not guaranteed by Geode**, so:

- `PlayLayer::destroyPlayer` is hooked in **exactly one place** (`ExtrasPlayLayer`, extras.cpp). It handles noclip swallowing, accuracy counting and Safe Mode in that order. Do not add a second one.
- `PlayLayer::resetLevel` is hooked by bot (macro trim / held-sync), practice (checkpoint restore, called *by* the bot hook at the end via `practice::applyPending`) and trail (clear). They chain via `PlayLayer::resetLevel()` calls; keep the bot hook's call to `practice::applyPending` **after** `PlayLayer::resetLevel()` so GD is done touching the player first.
- `GJBaseGameLayer::processCommands` is hooked by bot (record ticks, playback inputs, resume hand-over) and extras (autoclicker *before* the original call, cheat-flag + accuracy *after*).
- `GJBaseGameLayer::update` is hooked by hacks for the frame stepper (returns early without calling the original while frozen).
- `CCScheduler::update` is hooked by hacks for speedhack (scales `dt` before the original).

Timing contract with Eclipse/xdBot (do not break): inputs are recorded in `handleButton` with `frame = m_gameState.m_currentProgress`, and fired during playback right after `processCommands` for every input with `frame <= current`.

## Serialization formats

### Session file — `sessions/<levelID>.gdm`
```
"GDMS" u32 version(=3) i32 resumeFrame f32 percent u32 count
count * { i32 frame, u8 button, u8 down, u8 player2, u8 phys,
          f32 x, f32 y, f32 rot, f64 xVel, f64 yVel }
```
v2 (pre-2.4) omitted the phys block; `readSession` still accepts it.

### Fix file — `sessions/<levelID>.gdmf`
```
"GDMF" u32 version(=1) u32 count
count * { i32 frame, u8 hasP2, 2 * (f32 x, f32 y, f32 rot, f64 xVel, f64 yVel) }
```
Files written before v2.5.0 have no magic (raw count first); `readFixes` detects
the missing magic and rewinds to parse the legacy layout.

### Replay files — `.gdr2` / `.gdbot`
Standard [GDReplayFormat v2](https://github.com/maxnut/GDReplayFormat) with the
standard `"Phys"` input extension. The replay-level extension (botInfo name
`"GDMenu"`, version >= 4) stores the per-tick fix list so a GDMenu file can be
resumed/drift-corrected by GDMenu alone; other bots ignore it.

## The safety rule for untrusted bytes

**Never trust a count read from disk.** gdr's `importData` reserves vectors with
counts from the file and its reader stalls (instead of failing) on truncated
varints, so a 100-byte forged file can OOM or hang the game. Therefore:

1. `gdm::prevalidateGdr()` dry-runs gdr's exact header walk first and enforces
   (a) every counted loop fits in the bytes that remain and (b) every read
   advances the stream. Only then is `importData` allowed to run.
2. `gdm::safeImport()` wraps that in try/catch as a last resort.
3. Session/fix readers bound every loop by `remaining bytes / record size`.

All of this is regression-tested: `tests/test_replay_io.cpp` includes forged
counts, truncations, stalled varints and two 4000-buffer fuzz loops, run plain
**and** under ASan+UBSan in CI.

## Adding a feature

1. Pure logic / bytes? Put it in `src/core/replay_io.hpp` (or a new core header)
   and add tests in `tests/`.
2. Game behaviour? A `$modify` class next to related hooks; check the hook map
   above for ownership conflicts first.
3. UI? A tab builder in `src/ui.cpp`; use `toggleRow` / `stepperRow` (pass a host
   node when inside a ScrollLayer) and keep everything behind the pause bubble.
4. Setting? Declare it in `mod.json`; read it where used; it reloads live via
   `listenForAllSettingChanges` automatically if it affects `hacks::reloadSettings`,
   otherwise read it per use (values are cached by Geode).
5. Run `bash tests/run_tests.sh` (and `SANITIZE=1 ...`) before committing.
