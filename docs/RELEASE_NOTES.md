# SACore release notes

## 1.2.x and 1.3.x

The QoL mod set (overview with keys: docs/MODS.md):

- Saves: autosave after every passed mission (own rotating files), load the newest save at start (hold Shift for
  the menu), import community saves from `sacore\import` into a menu slot (Saves page of the F11 console).
- Story: mission retry after a fail (F4 back to the marker); story runner in the mission pass mod (`run_to`: plays
  the whole story up to a chosen mission, or to its end, and stops next to the marker; `save_to` saves that point
  into a menu slot); trip skip on long mission drives.
- Story runner without pauses (1.3.0): it answers the contacts' phone calls, spawns a lowrider where the story
  wants one, wins the races through Rockstar's race skip, buys Zero's RC shop, Wang Cars and the airstrip, gets
  Millie's keycard, and gives Grove Street the territory End of the Line asks for. Optional strands (driving school,
  Zero, Wang Cars, the heist) are played where the story allows them, so every story mission is a target.
- Story saves (1.3.0): the runner saves every story point it passes (one save per mission, kept apart from your own
  saves); choosing a mission you are already past, or from a fresh game, loads that save at once, otherwise the
  runner plays on from your game. `tools/saves.py story-copy` takes the story saves of another install.
- Stability (1.3.0): no crash when all 96 script threads are in use and an object's script brain starts (casino
  interiors start some 55 at once); the object then simply has no brain until it streams in again.
- World and player: time and weather controls, police switches, stat multipliers, density and draw distance,
  more streaming memory, vehicle tweaks (no roof damage, mouse steering recenter), weapons only by key.
- Presentation and input: HUD toggle and photo mode, radar zoom and GPS route line, weapon and radio wheel,
  aim sensitivity and raw mouse, Hor+ field of view, frame limit up to 60 FPS, XInput controllers on 1.0 US.
- High frame rates (1.2.28): above 30 FPS the frame_rate mod corrects the game's frame-dependent physics and timing
  on both builds (41 sites on 1.0 US, 38 on newsteam): swimming and diving speed, doors, buoyancy, boat speed,
  Skimmer take-off, burnout, coasting, siren by horn tap, heli rotor spin-up, pushing parked cars, climbing
  deaths, vehicle turning, water cannon, AI car timing. Swimming at 60 FPS now covers the same distance as at 30
  (measured). Scripts with their own per-frame logic run with a lower limit (pool, dates, gyms, Fender Ketchup at
  30, the crack den in Just Business at 50): their logic is in the mission scripts, not in the exe, so an exe patch
  cannot fix it.
- Mod API (core 1.2.0): memory patches through the patch manager, core settings, save import.
- Preload (1.3.2): with the 4 GB flag in the play copy's exe (`tools/laa.py`, never the Steam folder), the
  streaming_memory mod loads every world model and texture once in the background and keeps it, so nothing pops in
  again (about 2 minutes after a new game or a load, 883 MB). It always lets the game's own loading go first.
- Phone calls (1.3.3): cutscene_skip ends phone calls with the skip key (`skip_calls = "auto"`: every call right
  after it is answered) and can answer them (`answer_calls`). A skipped call still unlocks what it announces.
- Stability (1.3.4): no more rare freeze for good while loading, on both builds. A race in the game's own stream
  code (CdStreamSync) could miss the end of a read.
- Background play (1.3.5): a game running in the background no longer opens its pause menu when it gets the focus
  back.

## 1.1.x

- Eight menu slots plus separate quick save and auto save files; mods can save, list and load saves by name.
- Quick save mod: F5 quick save (free roam only), F9 twice quick load.
- Mission pass mod: F6 twice passes the current story mission (Rockstar's own debug skip, or a jump into the
  mission's pass code); also finishes the opening "In the Beginning" once CJ has control.
- Cutscene skip mod: Enter skips the current cutscene or skippable scripted scene; `auto_skip` skips all of them,
  including the intro of a new game. Settings in the F11 console.

## 1.0.0

First complete version: stable base for playing, framework for mods, tooling for both supported builds.

Supported game builds:
- `newsteam_r2_lv`: Steam `gta-sa.exe` (newsteam r2, German low-violence, SHA-256 `aa07fcdc...5034fe3`)
- `us10_hoodlum`: 1.0 US `gta_sa.exe` (Hoodlum, SHA-256 `a559aa77...0645cbd26`), made with `tools/apply_downgrade.py`

Bundled components: Ultimate ASI Loader 9.7.4, SilentPatch SA 34.1. Linked: MinHook 1.3.4, toml++ 3.4.0,
Dear ImGui 1.92.9b (devtools only).

### For playing
- Windowed mode with a normal frame at exactly the game's resolution (or a configured width), no flicker at
  start; the game keeps running in the background without trapping or moving the mouse; optional start muted;
  option to start behind other windows.
- Startup fixes: Device Selection dialog confirmed automatically (multi-monitor systems), intro movies skipped.
- Crash handler: report naming the faulting module plus a minidump in `sacore\crashes`.
- Saves: backup before every save (10 per slot by default), validation after each save and at start, separate
  User Files per install (on by default for the 1.0 US copy), restore tool.
- Installer with dry-run, update, verify, uninstall, safe mode (all mods off) and rollback.
- Fixes found while testing: "new game" from the pause menu (test copies), the crash on exit after a new game,
  settings file outside the separate folder (1.0 US), F10/Alt freezing the window, Shift typed elsewhere reaching the game.
- One crash package (zip) per crash, written silently; `tools/triage_crash.py` turns it into a named call chain.
- F11 console in the game's look (its own fonts and colours), scaling with the resolution, with live settings for every mod.

### For mods
- Mod API version 1 (C ABI and C++20 wrapper): logging, capabilities, events (frame, state, save/load, device,
  shutdown), player state, money, typed per-mod settings, side-car save data, hooks through the patch manager,
  content overrides, content-only mods. A crashing mod is disabled, the game keeps running.
- Tools: mod generator, deploy, launcher, packaging, compatibility check, mod manager with profiles,
  runtime test harness, crash symbolizer, log bundle.
- Mods: `devtools` (in-game console, F11), `session_stats` (example mod).

### Verified
- Unit tests (doctest) and tool tests (unittest) pass.
- Runtime suite on both builds through the in-game harness, including a 9-minute soak with saves every 2 minutes
  (no memory growth), failure tests (crashing mods, broken settings, a killed game process) and every automated
  regression (`tests/runtime/regressions.md`).

### Known limitations
- Modloader 0.3.10 does not work on either build; use SACore content mods.
- Single entries inside IMG archives cannot be replaced (whole files and whole archives can).
- Rendering, and with it frame events and the test harness, pauses while the game window is minimized.
- Checks that need a human are listed in `docs/COMPATIBILITY.md`, Manual checks.
