# GDMenu architecture

How the mod is put together, where things live, and the rules we follow when changing it.

## File map

| File | Responsibility |
|---|---|
| `src/core/replay_io.hpp` | **All byte-level I/O.** Session files, per-tick fix files, GDR2 replay types, file-name sanitising, and the corrupt-file validator. Geode-free on purpose: it compiles and tests on a desktop compiler (`tests/`). |
| `src/core/clips.hpp` | **Always-on attempt clip ring** (`gdm::Clip`, `gdm::ClipRing`): bounded by count AND bytes, oldest-first eviction. Geode-free, unit-tested (`tests/test_clips.cpp`). |
| `src/state.hpp` | Shared POD state (`BotData`, `HackData`), the `bot::` / `clips::` / `hacks::` / `extras::` / `replays::` / `practice::` / `hud::` API surface. No logic. |
| `src/bot.cpp` | Recording / playback / resume engine + GDR2 file library + all `PlayLayer`/`GJBaseGameLayer` hooks that drive the bot + CBS/CBF compatibility management + quick respawn. |
| `src/clips.cpp` | Clip capture glue (owns **no hooks** - the existing bot/extras hooks call into it) + watch/save/delete actions for the Clips tab. |
| `src/hacks.cpp` | Noclip, speedhack (+ FMOD music pitch sync + standalone pitch shift), **physics bypass** (fixed-240-tps accumulator), hitboxes, frame stepper, start-pos switcher, **force platformer** (apply/restore), **auto practice**, **practice music bypass**, keybind press handling, settings reload. |
| `src/gamehacks.cpp` | Hook-target hacks that need their own classes: **no particles** (`ParticleGameObject::init`), **no pulse** (`colorForPulseEffect`), **all passable** (`collidedWithObject`), **jump hack + no wave trail** (`PlayLayer::postUpdate`), **unlock icons** (`isIconUnlocked`). |
| `src/extras.cpp` | Autoclicker, Safe Mode, noclip accuracy + per-player noclip + noclip limits, **free attempts**, themes, profiles, stats counters, and the **single** `destroyPlayer` hook. |
| `src/practice.cpp` | Checkpoint physics snapshot/restore so practice-mode recordings don't desync. |
| `src/hud.cpp` | Optional in-game overlay (off by default), incl. FPS / attempts / jumps / time / CPS / best-run / run-from counters. Run tracking API: `hud::noteRunStart` (every `resetLevel`) / `hud::noteRunEnd` (real deaths + completions). |
| `src/trail.cpp` | Optional flight-path drawing: rolling chunk draw-nodes, length from the `trail-length` setting (whole chunks age out - no flicker, no redraws). |
| `src/ui.cpp` | Floating bubble (incl. cheat indicator), tabbed panel (8 tabs), all popups (save/rename/sessions/intro). The **Hacks tab is table-driven**: `hackRows()` describes every row (title/desc/get/text/steps/act), `rebuildHacksList()` renders it through the search filter, and `onRowToggle`/`onRowStep` dispatch by button tag - adding a hack is one table entry + one mod.json setting. Owns the `g_menuOpenCount` global (how many GDMenu panels are open). |

## Hook map (who modifies what)

Multiple `$modify` classes target the same GD classes. Order between *different functions* doesn't matter; order between two hooks of the *same function* is **not guaranteed by Geode**, so:

- `PlayLayer::destroyPlayer` is hooked in **exactly one place** (`ExtrasPlayLayer`, extras.cpp). Order inside it: per-player noclip check + noclip limits -> swallow OR fall through; on a **real** death it calls `clips::onDeath` and `hud::noteRunEnd(percent)` (Best Run), then accuracy counting has already happened per tick, then Safe Mode test-mode wrapping. Do not add a second one. (`levelComplete` in bot.cpp mirrors this with `hud::noteRunEnd(100)`.)
- `PlayLayer::resetLevel` is hooked by bot (clip attempt-start when idle, macro trim / held-sync, **`hud::noteRunStart(getCurrentPercent())`** for the Run-From counter), practice (checkpoint restore, called *by* the bot hook at the end via `practice::applyPending`), extras (**free attempts**: `m_attempts = 1` after the original) and trail (clear). They chain via `PlayLayer::resetLevel()` calls; keep the bot hook's call to `practice::applyPending` **after** `PlayLayer::resetLevel()` so GD is done touching the player first.
- **Force-platformer ownership rule:** `GJGameLevel::isPlatformer()` is literally `m_levelLength == 5`, so `hacks::applyForcedPlatformer(level)` mutates that field **before** `PlayLayer::init` (HackPlayLayer hook, hacks.cpp) and stashes the original. The `GJGameLevel` object outlives the level (cached lists / search results), so `hacks::restoreForcedPlatformer()` **must** run when leaving - it is called from the bot's `onQuit` hook (bot.cpp), and `applyForcedPlatformer` also restores first as a belt-and-braces guard against re-entry without a quit.
- `GJBaseGameLayer::processCommands` is hooked by bot (record ticks, playback inputs, resume hand-over) and extras (autoclicker *before* the original call, cheat-flag + accuracy *after*).
- `GJBaseGameLayer::update` is hooked by hacks for the frame stepper (returns early without calling the original while frozen) and for **physics bypass**: an accumulator steps the original at a fixed `1/240 s` (max 16 substeps per frame, excess time is dropped so a tab-out spike can't spiral). The incoming `dt` is already speedhack-scaled, so speedhack composes with the bypass for free.
- `CCScheduler::update` is hooked by hacks for speedhack (scales `dt` before the original) and music pitch sync (`syncMusicAudio` - FMOD channel frequency, base captured once, restored when the hack ends or the level is left).
- `PlayLayer::postUpdate` (bot hook) owns **both** auto-restart paths: Loop Playback (state == Playing, 0.8 s) and Quick Respawn (state == Idle, setting-driven). They share one `deadTime` field and are mutually exclusive by state.
- `PlayLayer::postUpdate` is now hooked by **six** `$modify` classes (bot, hud, trail, hacks, extras, gamehacks) - all of them call the original, so they chain safely in any order. The gamehacks one hides the wave trail (`m_waveTrail->setVisible`) and implements the jump hack (force `m_isOnGround = true` while jump is held mid-air, so vanilla's own hold-to-jump keeps re-firing).
- **Single-target hooks owned by `gamehacks.cpp`** (nothing else may hook these): `ParticleGameObject::init` (no particles - new spawns only), `GJEffectManager::colorForPulseEffect` (no pulse - colour passthrough), `PlayerObject::collidedWithObject(float, GameObject*, CCRect, bool)` (all passable - the 2-arg overload is inline and forwards here, so this covers every solid-collision path), `GameManager::isIconUnlocked` (unlock icons - cosmetic only; server paths go through `GameStatsManager` and stay untouched).
- `PauseLayer::customSetup` is hooked in **exactly one place** (`BotAutosavePause`, bot.cpp): recording autosave + **hide pause** (schedules `autoResume` after 0.8 s, which calls `keyBackClicked()`). `autoResume` must never fire while `g_menuOpenCount > 0`, or the user can be locked out of GDMenu in-level.
- `clips.cpp` owns **no hooks at all**. Capture rides the existing ones: `handleButton` (bot hook, Idle only), `resetLevel` / `init` / `levelComplete` / `onQuit` (bot hooks), `destroyPlayer` (extras hook), and `setState` (bot) via `clips::onBotActive`.

**setState ordering rule (bot.cpp):** `clips::onBotActive` runs **before** `updateCBF` / `manageCBS`. Clips may hold their own CBS pause (setting `clips-pause-cbs`); finalizing the clip first restores that pause so the bot's CBS manager always saves/restores the *user's* value and the two never stack. `manageCBS` only ever writes `m_clickBetweenSteps` back to the **same layer pointer** it paused (never a stale one), and `updateCBF` only touches Syzzi's `soft-toggle` when `getSetting()` says the key exists in the installed CBF version.

Timing contract with Eclipse/xdBot (do not break): inputs are recorded in `handleButton` with `frame = m_gameState.m_currentProgress`, and fired during playback right after `processCommands` for every input with `frame <= current`. Clips use the exact same capture point and clock, which is why "Watch" can hand a clip straight to the playback engine.

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

## Attempt clips (in-memory only)

The always-on recorder keeps a `gdm::ClipRing` (core/clips.hpp) of finalized
attempts for the **current level** (cleared on level change, never written to
disk - `Save` in the Clips tab is the only way one becomes a `.gdr2`).

A clip = metadata (attempt #, %, frames, practice/completed/subframe flags,
timestamps) + `ClipInput{frame, button, down, player2}` events. No physics, no
per-tick data: capture happens inside `handleButton` only, so idle cost is zero.

Lifecycle (all in clips.cpp, driven by the hooks above):
`onLevelEnter` (ring reset on level change) -> `onAttemptStart` on every
`resetLevel` while idle (practice checkpoint respawn **trims** inputs at/after
the respawn frame instead of finalizing) -> `onInput` per button event (same
filters as bot recording: buttons 1-3, jump-only outside platformer, dead
players ignored, key-repeat deduped) -> finalized by `onDeath` (real deaths
only - noclip-swallowed hits don't end a clip), `onComplete`, `onQuit`,
`onBotActive(true)`, or the next `onAttemptStart`.

`subframe` flag: set when `m_clickBetweenSteps` (vanilla CBS) was on or CBF is
installed while capturing - tick-based playback of such clips can drift <1 tick,
so the UI badges them and warns on Watch.

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
counts, truncations, stalled varints and two 4000-buffer fuzz loops;
`tests/test_clips.cpp` covers the clip ring (caps, eviction, bounds). Both run
plain **and** under ASan+UBSan in CI (`bash tests/run_tests.sh`).

## Adding a feature

1. Pure logic / bytes? Put it in `src/core/replay_io.hpp` or `src/core/clips.hpp`
   (or a new core header) and add tests in `tests/`.
2. Game behaviour? A `$modify` class next to related hooks; check the hook map
   above for ownership conflicts first.
3. UI? A tab builder in `src/ui.cpp`; use `toggleRow` / `stepperRow` (pass a host
   node when inside a ScrollLayer) and keep everything behind the pause bubble.
   **New hack?** Don't touch the tab builders - add one `HackRow` entry to
   `hackRows()` (and the mod.json setting); the search bar, rendering and
   in-place updates come for free. If it's gameplay-affecting, also add its
   cached flag to `HackData` / `hacks::reloadSettings` and to
   `extras::cheatsActive()` so Safe Mode + the cheat indicator see it.
4. Setting? Declare it in `mod.json`; read it where used; it reloads live via
   `listenForAllSettingChanges` automatically if it affects `hacks::reloadSettings`,
   otherwise read it per use (values are cached by Geode).
5. Run `bash tests/run_tests.sh` (and `SANITIZE=1 ...`) before committing.
