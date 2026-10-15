# Compatibility Matrix

Only rows that were actually tested appear as "tested". Everything else says what is known and how.

## Builds

| Build id | Exe | MD5 | Status |
|---|---|---|---|
| `newsteam_r2_lv` | `gta-sa.exe` 5,971,456 bytes | `d9cb35c898d3298ca904a63e10ee18d7` | target; static profile done |
| `us10_hoodlum` | `gta_sa.exe` | `170b3a9108687b26da2d8901c6948a18` | target; made with `tools/apply_downgrade.py` |
| any other | | | unsupported: framework stays inert and logs the hash |

## Environment

| Item | Value |
|---|---|
| OS | Windows 11 Pro 10.0.26200 |
| GPUs | NVIDIA GeForce RTX 4090 (driver 32.0.16.1088), AMD Radeon iGPU |
| Monitors | 3 (1920x1080 primary 240 Hz, 2x 2560x1440) |

## Components

| Component | Version | newsteam_r2_lv | us10_hoodlum |
|---|---|---|---|
| Ultimate ASI Loader (as `dinput8.dll`) | 9.7.4 | installed; not launched before SACore's own windowed mode existed | loads ASIs from `scripts\` (runtime: module list, 2026-10-13) |
| SilentPatch | 34.1 | installed; not launched yet | loaded (runtime: module list) |
| III.VC.SA.WindowedMode | 2.2 (MIT) | unsupported (1.0 US only per README) | loaded; 640x480 window visible after focus (runtime) |
| SACoreProbe (early test plugin, superseded by SACore) | 0.1.x | loads; build detected; windowed 1280x960; intro skip; ~230 fps in menu (runtime) | same; ~233 fps in menu (runtime) |
| Modloader | 0.3.10 | does not work: blocking "Mod Loader" dialog, SACore never starts | loads, never initialises (no log, no files); disabled in both copies, SACore content layer instead |
| SACore | 1.3.5 | smoke: focus return leaves the game running (fails without the fix); new game with the monitors in power save runs (46/50 then, the 60 FPS checks need a lit display) | same |
| SACore | 1.3.4 | CdStreamSync replacement in use (smoke); full tier 72/74, no freeze | same |
| SACore | 1.3.3 | mod_phone_call_skip: Tenpenny's call skipped, Burning Desire unlocked | same |
| SACore | 1.3.2 | 4 GB flag set by tools/laa.py; world preload 14312 models in 127 s, 883 MB, 2.2 GB address space left | same |
| SACore | 1.1.5 | mods: cutscene_skip (auto and Enter), mission_pass (debug skip and pass-code jump on Big Smoke, end of In the Beginning), quicksave in free roam with quick load (money restored, clock unchanged); capabilities cutscene.skip, mission.pass, game.tick, game.mission.read, save.quick, save.load_slot, save.extra_slots, stats.read verified | same |
| SACore | 0.9.9 | installed by the release installer into a fresh copy: install_check, in-game restart with clean exit, crash package complete; uninstall restores the Steam files byte for byte | same (fresh downgraded copy) |
| SACore | 0.9.7 | suite 11 scenarios pass (config_values, content, crash, devtools, example_mod, incomplete_shutdown, mod_api, new_game_restart, patches_off, saves, smoke); soak 9 min PASS, memory flat | same suite pass; soak PASS on 0.9.2 (two later runs were interrupted at the game window: minimized, then a pause-menu new game) |
| SACore | 0.9.2 | runtime suite 8/8 scenarios pass (smoke, crash, patches_off, mod_api, devtools, saves, content, example_mod) | same |
| SACore | 0.8.1 | mouse not trapped or moved while the window is in the background; no focus grab with `focus_on_start = false` | same |
| SACore | 0.8.0 | runtime suite 6/6 scenarios pass, saves written through the game and validated, separate User Files folder | same |
| SACore | 0.4.0 | runtime suite 3/3 scenarios pass; frame work average 56 us | runtime suite 3/3 pass; frame work average 50 us |
| SACore | 0.3.0 | crash handler: deliberate crash -> report + minidump naming `SACore.asi+0x4a8e0`, stack through SilentPatch, process exits without a Windows dialog; Device Selection confirmed by the core | crash report + minidump (same) |
| SACore | 0.2.2 | build detected, all 10 capabilities resolved (8 supported, 2 experimental); windowed, background (1452 frames in 39 s with focus loss at 12 s), mute, intro skip, auto start, money write, player read, load event fires | same (load event fires) |

## Runtime baselines (automated runtime tests, 2026-10-13, `tools/launch_test.py`)

| Copy | Stack | Result |
|---|---|---|
| steam30 (newsteam_r2_lv) | vanilla | starts without Steam; Device Selection dialog confirmed automatically; fullscreen game window after 1.3 s |
| us10 (us10_hoodlum) | vanilla | Device Selection dialog confirmed; fullscreen game window after 4.8 s |
| us10 | UAL + SilentPatch + WindowedMode | no Device Selection dialog; window stays hidden until it gets focus (launcher must activate it; a user double-click does that); then 640x480 window. Whether rendering progresses past the intro movie: measured with the Present-hook probe (next rows) |
| us10 | UAL + SilentPatch + WindowedMode | stuck in `gGameState` 2 (logo movie) for 100+ s (memory read); WindowedMode replaced by SACoreProbe windowed mode, plugin file renamed to `.asi.off` |
| both | UAL + SilentPatch + SACoreProbe | intro skipped (P1), windowed 1280x960 with frame, rendering 229 to 237 fps in the main menu for 40 s (Present-hook counter) |
| us10 (exe with 1 changed header byte) | UAL + SilentPatch + SACoreProbe | probe logs `UNKNOWN (unsupported build, probe stays inert)`, no device hooks, no writes (failure test) |

## Known problems

| Problem | Builds | Status |
|---|---|---|
| "Device Selection" dialog blocks startup on multi-adapter / multi-monitor systems | both | fixed in SACore 0.3.0 (`stability.skip_device_selection`), runtime verified on newsteam |
| Intro movies never finish (DirectShow), game stays in logo state | us10 observed; newsteam presumably | worked around by P1 (skip intro); root cause not investigated |
| DirectPlay install prompt on start | all (vanilla) | fixed by SilentPatch (manual check, see below) |
| Shared User Files folder between builds (saves not portable: newsteam id 0xFD148DF6, 1.0 US id 0x35DA8175) | both | fixed by `[saves] separate_user_files` (0.8.0); saves and settings go to `sacore\userdata`; the game's first lookup (before plugins load) still creates an empty `GTA San Andreas User Files` folder in Documents (checked 2026-10-13: empty) |
| Fullscreen start moves windows off the main monitor and blacks out other monitors | both (vanilla) | avoided by `[window] windowed = true` (default in SACore installs) |
| Background running made the game trap and move the mouse | both | fixed in 0.8.1 (mouse guards, R16) |
| Window frame flickered off and on and the window jumped from 640x480 to 1280x960 at start (the game sets WS_POPUP after device creation) | both | fixed in 0.9.3: the game's own SetWindowLongA/SetWindowPos on its window are absorbed (R17); the window follows the game resolution |
| "Start new game" from the pause menu crashed on test copies | both | fixed in 0.9.6 (R19); only test copies with `auto_start_new_game` were affected |
| Crash while closing the game after a new game (RenderWare teardown) | both (vanilla behaviour) | fixed in 0.9.6 by `game.exit_crash_guard` (R20) |
| Crash in a casino interior when all 96 script threads are in use (object brains start together) | both (vanilla behaviour, seen with the story runner on 1.0 US) | fixed in 1.3.0 by `game.script_thread_guard` |
| Settings file (gta_sa.set) ignored the separate User Files folder | us10 | fixed in 0.9.6 (`saves.user_files_folder`, R21) |
| F10 or Alt froze the windowed game; Shift typed elsewhere moved the player in the background | both | fixed in 0.9.10 (R22, R23) |
| Rendering, frame events and the test harness pause while the game window is minimized | both | game behaviour; logged ("minimized", "minimize requested by the user") |
| Modloader 0.3.10 unusable | both | not fixable on our side; use SACore content mods (`docs/API.md`, Content) |
| Replacing single files inside IMG archives | both | not supported; whole files and whole archives can be replaced |

## Troubleshooting

| Symptom | Check | Fix |
|---|---|---|
| Game does not start, nothing in `sacore\logs` | `dinput8.dll` present? `scripts\SACore.asi` present? | reinstall (`docs/INSTALL.md`, dry-run first) |
| Log says `UNKNOWN (unsupported build)` | `python tools/inventory.py --path <install>` shows the exe hash | only the two builds above are supported; restore the exe from the pinned copy (`docs/INSTALL.md`) |
| A capability is `Unsupported` in `sacore\diagnostics\capabilities.txt` | the reason column names the failed signature | the exe differs from the profiled one; features depending on it stay off, the game still runs |
| Game crashes | `sacore\crashes\<time>.zip` is the crash package (report, dump, log, settings) | send the zip; analysis: `python tools/triage_crash.py <zip>` |
| Game crashes (details) | `sacore\crashes\<time>.txt` names the module | SACore offset: `python tools/symbolize.py <SACore.map> 0x<offset>`; a mod DLL: disable it with `python tools/modman.py --game <install> --apply disable <mod>` |
| Suspect a SACore patch | `sacore\diagnostics\patches.txt` | add its id to `core.toml [patches] disabled` and restart |
| Suspect any mod | | disable mods with `python tools/modman.py --game <install> --apply disable <name>` (see `list`), or apply a profile saved earlier with `--apply profile apply <name>`; re-enable one by one |
| Device Selection dialog appears | `core.toml [stability] skip_device_selection` | set it to `true` |
| Game starts in 640x480 on a PC with several monitors | `core.toml [stability] device_selection_resolution` | set the resolution, for example `"1920x1080"`; the log line "Device Selection: resolution ..." says what was chosen or lists what the dialog offered |
| Saves missing after switching builds | each install has its own `sacore\userdata\GTA San Andreas User Files` | intended (save formats differ between builds); copy only between installs of the same build |
| Save damaged | `python tools/saves.py --game <install> list` | `python tools/saves.py --game <install> --apply restore <slot> <backup>` (names from `backups <slot>`) |
| Need help from someone else | | `python tools/collect_logs.py --game <install> --out <folder>` (no saves, no personal files; paths may contain the Windows user name) |

## Manual checks (need a human)

| Check | Why it needs a person at the game |
|---|---|
| Graphics look right (no missing textures, flicker, wrong aspect) in windowed mode | visual judgement |
| Mouse and keyboard feel normal in menus and driving; mouse free after leaving the menu (R11) | input feel |
| Save through the menu, quit, load through the menu: game continues, `session_stats` totals restored | menu navigation by hand |
| Audio: `start_muted = false` gives sound, `true` gives silence | hearing |
| DirectPlay prompt does not appear with the full stack (R8) | needs a fresh Windows profile or a machine without DirectPlay |
| Skimmer, interiors, long drive across the map (R9, R10) | gameplay |
| Developer console (F11) readable and usable | visual judgement |
| Change the resolution in Options, Display Setup: the window follows it and stays sharp | menu navigation by hand |
| QoL mods, feel and look: photo mode camera (W A S D, Q E, mouse), weapon and radio wheel (hold Caps Lock, mouse direction), GPS line on the radar, Hor+ view on a wide window, HUD toggle | visual judgement and held keys (the harness only sends key presses) |
| QoL mods, gameplay: stat gains faster (sprint, gym), car on its roof does not burn, weapons only by the pickup control, mouse steering recenters, density and draw distance sliders, trip skip prompt on a long mission drive, XInput pad on 1.0 US | needs real play (stats, physics, input devices) |
| After a story runner run or a mission pass jump: screen faded in, CJ controllable, camera behind CJ | visual check (the jump fades in and restores control through the mission's own commands) |
| After a long story runner run: the map shows the expected contacts and properties, the next mission starts from its marker and plays normally | gameplay (the runner skips the missions' bodies, so items a mission would give on the way are not given) |
