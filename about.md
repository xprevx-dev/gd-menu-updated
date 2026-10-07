# GDMenu

A **replay bot** and practice toolkit for Geometry Dash, made for **both PC and mobile**.

## <cy>The Bot</c>
- **Record & Play** your inputs, frame-perfect (240 ticks per second).
- **Resume where you left off**: quit while recording and come back later. The bot fast-forwards to your exact frame, freezes there, and you keep recording. The **Sessions** manager lists every saved resume point, for every level.
- **Loop Playback** watches a bot run on repeat; **Stop At %** drills one section.
- **Practice mode support**: exact physics when you respawn at a checkpoint, plus per-tick position/speed so playback can't drift.
- **Save as <cg>.gdr2</c> or <cg>.gdbot</c>** (same GDR2 bytes, standard "Phys" extension) - your bots work in other GDR2 bots, and one tap copies them into Eclipse's folder.
- **Your bots library**: search it, sort it, rename entries, see length/size/physics at a glance. Corrupt files can't crash the game - they just show as unsupported.
- **Click Between Steps** (vanilla) and **Click Between Frames** (Syzzi) are paused automatically while the bot runs, and restored after - sub-tick inputs would desync any tick-based replay.

## <cy>Attempt Clips</c>
- GDMenu **records every attempt, all the time** - inputs only, so it costs zero performance.
- **Clips tab**: your last runs with % / time / inputs / badges. **Watch** replays one live through the bot, **Save** keeps it as a standard <cg>.gdr2</c>.
- Keeps the last N attempts (default 5, 0 = off) in a memory-capped ring - oldest gets evicted first.

## <cy>Tools & Hacks</c>
- **Frame Stepper**: freeze the game and move one tick at a time (touch buttons on phones).
- **Noclip** (per player, with optional **hit limit** and **accuracy floor**), **Speedhack** (0.1x-5x, optionally **pitching the music** with it), **Show Hitboxes** (+ forced **on death**), **Start Pos Switcher**, **Quick Respawn** (auto-retry after 0.1-3 s).
- **Player Trail** draws your flight path; the HUD's **input viewer** shows held buttons live, plus optional **FPS / attempts / jumps / time** counters.
- **Cheat Indicator**: the bubble turns red and says CHEATS while any hack is on.
- Every action has a **real keybind**: capture any key, combo or mouse button in Settings.

## <cy>More & Style</c>
- **Autoclicker** with adjustable clicks per second (recorded by the bot like real clicks).
- **Safe Mode** (on by default): cheated attempts never save percent or completions.
- **Noclip Accuracy** counter (optional).
- **In-game HUD** (optional, off by default): state, frame, percent, speed, accuracy in a corner you pick.
- **Themes**, bubble opacity/size and **preset profiles**.

## <cy>How to open it</c>
Tap the round **GDM bubble**. It floats on **every screen** and hides while you're playing (unless the HUD is on). Drag it anywhere - it remembers.
