# GTA San Andreas mods (SACore)

A modding framework and a set of comfort and quality-of-life mods for the classic PC version of
**Grand Theft Auto: San Andreas**. The framework, **SACore**, makes the game stable to play and easy to mod; 22 mods
on top of it add quick saves, autosaves, cutscene and phone call skipping, a story runner that plays the story up to
any mission, high frame rate fixes, a weapon wheel, a GPS route line, a preload of every model with the 4 GB address
space and more. Everything is configurable live in an in-game console.

> Unofficial fan project. Not affiliated with or endorsed by Rockstar Games or Take-Two Interactive. You need your
> own copy of the game; no game files are included. All game names and trademarks belong to their owners.

## Contents

- [Supported game builds](#supported-game-builds)
- [Features](#features)
- [Install](#install)
- [Using the mods](#using-the-mods)
- [Saves, crashes and recovery](#saves-crashes-and-recovery)
- [Troubleshooting](#troubleshooting)
- [For developers](#for-developers)
- [Tests](#tests)
- [Tools](#tools)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [Credits](#credits)
- [License](#license)

## Supported game builds

SACore recognises the game executable by its SHA-256 hash and only acts on these two builds. Any other executable is
detected and left untouched (the log says why).

| Build id | Executable | Notes |
|---|---|---|
| `newsteam_r2_lv` | Steam `gta-sa.exe` (newsteam r2, German low-violence, buildid 459558) | the current Steam release |
| `us10_hoodlum` | 1.0 US `gta_sa.exe` (Hoodlum, SHA-256 `a559aa77...0645cbd26`) | made from the Steam files with the GTA SA Open Downgrader patches (`tools/apply_downgrade.py`) |

A copy whose executable has the Large Address Aware (4 GB) flag set is still its build: the hash is taken with that
flag cleared. Per-build addresses are checked against byte patterns at start-up; a feature whose addresses do not
check out is switched off instead of risking a crash ([docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)).

SACore is always installed into a **copy** of the game, never into the Steam folder: every tool refuses a path inside
`steamapps`. Steam can then update or verify its own files without touching your mods.

## Features

### Framework (always on)

- **Stability**: fixes for the crash on exit, for "new game" from the pause menu, for running out of script threads
  in casino interiors, and for a rare freeze for good while loading (a race in the game's stream code). On top of
  [SilentPatch](https://github.com/CookiePLMonster/SilentPatch).
- **Windowed play**: a normal window frame at the game's resolution, no flicker at start, keeps running in the
  background without trapping or moving the mouse, no pause menu popping up when the window gets the focus back,
  optional start muted, the Device Selection dialog confirmed automatically on multi-monitor systems, intro movies
  skipped.
- **Saves**: a backup before every save (10 per slot by default), validation after every save and at start, eight menu
  slots plus separate quick save, autosave and story save files, and a separate User Files folder per install so the
  two builds never overwrite each other's saves.
- **Crash handler**: one zip per crash in `sacore\crashes` with a report naming the faulting module and call chain, a
  minidump, the session log and the settings; written silently, no Windows error dialog.
- **Mod loader**: mods are DLLs with a `mod.toml` manifest (settings with types, ranges and descriptions,
  dependencies, minimum core version). A crashing mod is isolated and reported, not taken down with the game.
- **In-game console (F11, `devtools` mod)**: status, mods, every setting of every mod editable live, saves and save
  import, log, crashes.

### Mods

Every setting can be changed live in the F11 console (Settings page); keys are Windows key codes and can be changed
there as well. Details per mod: [docs/MODS.md](docs/MODS.md) and `mods/<id>/README.md`.

| Mod | What it does | Default keys |
|---|---|---|
| `quicksave` | Quick save in its own rotating files (free roam only), quick load | F5, F9 twice |
| `autosave` | Saves after every passed mission (optionally also on a timer) in its own rotating files | - |
| `load_last` | Loads the newest save when the game reaches the main menu | hold Shift: stay in the menu |
| `cutscene_skip` | Skips cutscenes and skippable scripted scenes, or all of them automatically; ends phone calls (they still count for the story) and can answer them | Enter |
| `mission_pass` | Passes the current mission; the **story runner** plays the story up to any of 101 missions or to the end | F6 twice |
| `mission_retry` | After a failed mission: back to the mission marker | F4 |
| `photo_mode` | HUD on/off; photo mode with a slowed world and a free camera | F8, F7 |
| `stat_boost` | Faster stamina, muscle, lung capacity, driving and weapon skill gains | - |
| `time_weather` | Freeze or speed up the clock, set the hour, lock the weather | numpad + (one hour) |
| `police` | Never wanted, wanted level cap, faster decay, no helicopter shots, no police helicopters | - |
| `vehicle_tweaks` | No damage when a car lands on its roof; mouse steering returns to the centre | - |
| `weapon_pickup` | Weapons lying on the ground are picked up only with the pickup control | - |
| `mouse_aim` | Separate aim sensitivity, per-axis invert, raw mouse input | - |
| `world_density` | Pedestrian and traffic density, draw distance beyond the menu slider | - |
| `radar` | Radar zoom; GPS route line to the map waypoint | (zoom key off) |
| `wheel` | Weapon wheel on foot, radio wheel in vehicles, with slow motion | hold Caps Lock |
| `teleport` | Teleport to the map waypoint; fast travel between owned safehouses | F2, F3 |
| `trip_skip` | The game's own trip skip on long mission drives | (game prompt) |
| `frame_rate` | Frame limit up to 60 FPS with physics and timing fixes above 30 FPS; a lower limit where scripts break | - |
| `widescreen` | Hor+ field of view on wide screens | - |
| `controller` | XInput controllers on the 1.0 US build (the Steam build reads them itself) | - |
| `streaming_memory` | 256 MB streaming budget instead of 50 MB (less pop-in); with the 4 GB flag every world model and texture is preloaded and kept | - |
| `session_stats` | Play statistics; also the example mod for developers | - |

Highlights:

- **Story runner** (`mission_pass`, setting `run_to`): choose a mission and the runner plays the story up to it from a
  new game or from your save. It passes every mission before it, answers the contacts' phone calls, buys the
  properties the story needs, wins the races and stops next to the chosen mission's marker. Every story point it
  passes is saved as a story save (separate from your saves), so choosing a mission you are already past, or starting
  from a fresh game, loads that point at once. `save_to` writes the result into a menu slot.
- **High frame rates** (`frame_rate`): above 30 FPS the game's frame-dependent physics and timing are corrected on
  both builds: swimming and diving speed, doors, buoyancy, boats, Skimmer take-off, burnout, coasting, siren by horn
  tap, helicopter rotors, pushing parked cars, climbing, vehicle turning, water cannon, AI car timing. Scripts with
  their own per-frame logic (pool, dates, gyms, Fender Ketchup, the crack den) run with a lower limit.
- **Preload** (`streaming_memory`, setting `preload`): with the 4 GB flag set (see below) all 14312 world models with
  their textures load once in the background (about 2 minutes after a new game or a load, 883 MB) and are kept, so
  nothing pops in again anywhere on the map. The game's own loading always goes first.
- **Phone calls** (`cutscene_skip`, settings `skip_calls`, `answer_calls`): the skip key ends a call at once through
  the game's own skip, or every call ends right after it is answered; the mission or unlock the call announces still
  follows.
- **Community saves**: save files dropped into `<game>\sacore\import` are listed and checked on the console's Saves
  page and copied into a menu slot on request (the replaced save is backed up).

## Install

There is no ready-made download: you build SACore and the installer package yourself (about 10 minutes the first
time). Requirements:

- Windows 10 or 11, Python 3.11 or newer, git
- Visual Studio 2022 or 2026 with "Desktop development with C++" (MSVC x86 toolset), CMake 3.25 or newer
- Your own GTA San Andreas (the Steam version), about 5 GB free per play copy

### 1. Make a play copy

Close the game and copy the whole Steam folder somewhere else:

```bat
robocopy "C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto San Andreas" "D:\Games\GTASA-Play\steam" /E
```

The copy runs without Steam. Optionally make a second copy and downgrade it to 1.0 US (most classic mods need it),
see [docs/INSTALL.md](docs/INSTALL.md) section 2.

### 2. Build

```bat
git clone https://github.com/KeinNameVorhanden/gta-san-andreas-mods.git
cd gta-san-andreas-mods
cmake --preset x86
cmake --build --preset x86-release
```

CMake fetches MinHook, toml++, Dear ImGui and doctest at pinned versions; nothing third-party is committed.

### 3. Build the install package

The package bundles the two components SACore runs with. Download their release archives into `downloads\`:

| Archive | From |
|---|---|
| `downloads\ual\Ultimate-ASI-Loader-v9.7.4.zip` | [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) 9.7.4 |
| `downloads\silentpatch\SilentPatchSA-34.1.zip` | [SilentPatch](https://github.com/CookiePLMonster/SilentPatch/releases) for San Andreas, 34.1 |

```bat
python tools/package_release.py
```

This writes `dist\SACore-<version>\` (and a zip of it) with `install.py`, a hash manifest and the payload.

### 4. Install into the play copy

From the package folder, first a dry run, then the real install:

```bat
cd dist\SACore-1.3.5
python install.py --game "D:\Games\GTASA-Play\steam" install --with devtools,session_stats
python install.py --game "D:\Games\GTASA-Play\steam" --apply install --with devtools,session_stats
python install.py --game "D:\Games\GTASA-Play\steam" verify
```

Installed: Ultimate ASI Loader (`dinput8.dll`), SilentPatch and SACore (`scripts\`), and the optional mods you
picked. A file that was already there is backed up and comes back on uninstall. The installer refuses the Steam
folder, unknown executables and a running game. Every step is a dry run until `--apply`.

### 5. Add the mods

Package each mod you want and install it with the mod manager (dry run first, then `--apply`):

```bat
python tools/package_mod.py quicksave
python tools/modman.py --game "D:\Games\GTASA-Play\steam" install dist\quicksave-1.0.0.zip --apply
python tools/modman.py --game "D:\Games\GTASA-Play\steam" list
```

The mod manager also enables and disables SACore mods, ASI and CLEO plugins, saves and applies profiles, and checks
dependencies; it never deletes anything (removed files go to `sacore\backups\`).

### 6. Optional: the 4 GB address space

The game is a 32-bit program and gets 2 GB of address space. With the Large Address Aware flag set in its executable
it gets 4 GB on 64-bit Windows, which the preload needs. Set it in the play copy only, with the game closed:

```bat
python tools/laa.py --game "D:\Games\GTASA-Play\steam"
python tools/laa.py --game "D:\Games\GTASA-Play\steam" --apply
```

The original executable is kept in `sacore\install\backups\`; `--remove --apply` clears the flag again.

### Update, recover, uninstall

```bat
python install.py --game "D:\Games\GTASA-Play\steam" --apply update
python install.py --game "D:\Games\GTASA-Play\steam" --apply recover safe-mode
python install.py --game "D:\Games\GTASA-Play\steam" --apply recover rollback
python install.py --game "D:\Games\GTASA-Play\steam" --apply uninstall
```

`safe-mode` switches all mods off (`recover normal` turns them on again), `rollback` returns to the previous SACore
version. Uninstall restores the files SACore replaced and keeps your saves and settings. Full guide:
[docs/INSTALL.md](docs/INSTALL.md).

## Using the mods

Start the game by double-clicking the executable in the play copy. Expected: a window with a frame, no intro movies,
the main menu.

- **F11** opens the console. The Settings page lists every mod's settings with their descriptions; changes apply at
  once and are saved.
- Settings files: `sacore\config\core.toml` (framework) and `sacore\config\mods\<id>.toml` (one per mod), created
  with every setting and its description on first start. A broken file is never fatal: the defaults are used, your
  file is kept as `*.broken` and the log says why.
- Log: `sacore\logs\sacore.log` begins with the detected build and the state of every capability and ends each
  session with a summary.

Common framework settings in `core.toml` (all listed with descriptions in the file):

| Setting | Default | Effect |
|---|---|---|
| `[window] windowed` | true | windowed mode with a normal frame |
| `[window] run_in_background` | true | the game keeps running when another window has the focus |
| `[window] focus_on_start` | true | bring the window to the front when it opens |
| `[audio] start_muted` | false | start with the game's audio session muted |
| `[game] skip_intro_movies` | true | skip the logo and intro movies |
| `[saves] backups_per_slot` | 10 | backups kept per save slot (`sacore\save-backups`) |
| `[saves] separate_user_files` | false (the installer sets it for 1.0 US copies) | own User Files folder inside the play copy |
| `[stability] crash_dumps` | true | write a minidump with every crash report |
| `[mods] enabled` | true | load mods at all (safe mode switches this off) |
| `[patches] disabled` | "" | switch single SACore patches off by id (`sacore\diagnostics\patches.txt`) |

### Story runner

1. In the F11 console open Settings, `mission_pass`, and choose a mission in `run_to`.
2. From free roam or a new game the runner starts at once and shows its progress in the game's help box.
3. It stops next to the mission's marker: walk in to start it. Set `run_to` back to `off` to stop early.

Story saves (`GTASAst<N>.b`) live next to your saves but never appear in the menu or in `load_last`.
`tools/saves.py story-copy --from <other install>` copies the story saves of another install of the same build.

## Saves, crashes and recovery

| Situation | What to do |
|---|---|
| The game crashed | the zip in `sacore\crashes\<time>.zip` has everything; `python tools/triage_crash.py <zip>` names the module, the call chain and what to look at next |
| A save is damaged | `python tools/saves.py --game <copy> backups <slot>`, then `--apply restore <slot> <backup>` |
| The game misbehaves after adding a mod | `python install.py --game <copy> --apply recover safe-mode`, later `recover normal` |
| A new SACore version misbehaves | `python install.py --game <copy> --apply recover rollback` |
| Anything else | `python tools/collect_logs.py --game <copy> --out diagnostics.zip` bundles logs, settings and crash reports into one zip |

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| Nothing changes after installing | Check `sacore\logs\sacore.log`. No log at all: the ASI loader did not load (another `dinput8.dll`, or the file was blocked by Windows). |
| "unsupported build" in the log | The executable is not one of the two supported builds (another release, or a modified exe). SACore stays inactive on purpose. |
| A feature says "experimental" or "unsupported" | Its addresses did not check out on this executable (often another patch changed the code); the feature is off. |
| Modloader or III.VC.SA.WindowedMode installed | Both break this setup (Modloader blocks start-up on the Steam build); the installer warns about them. Use SACore's windowed mode and content folders instead. |
| Saves from the other build do not load | The two builds use incompatible save formats; each copy has its own save folder. |
| Pop-in with the preload on | The preload needs the 4 GB flag (`tools/laa.py`); the log line "4 GB address space" confirms it. |

More in [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) (tested builds and components, known problems).

## For developers

### Write a mod

```bat
python tools/new_mod.py my_mod --name "My Mod" --author you
cmake --preset x86
cmake --build --preset x86-release --target my_mod
python tools/deploy_dev.py --game "D:\Games\GTASA-Test\steam" --mod my_mod --apply
```

`new_mod.py` creates `mods/my_mod/` with a manifest, a CMake project, a C++ entry point and a README. Every folder
under `mods/` with a `CMakeLists.txt` is built automatically.

The mod API is a C ABI (`src/api/include/sacore/sacore_api.h`, version 1) with a header-only C++20 layer
(`sacore.hpp`, plus `sa_game.hpp` with shared game-state helpers):

- **Events**: frame, game tick, game state change, save and load started/completed, device created/reset, config
  changed, shutdown; key bindings that follow the user's settings.
- **Settings**: typed settings declared in `mod.toml` (bool, int, double, string, choice) with ranges and
  descriptions; live changes from the console; settings of other mods readable and writable.
- **Game access**: player state, capabilities per build (`Has("cutscene.skip")`), symbols and game functions by
  name, never raw addresses in mod code.
- **Patching**: hooks and memory patches through the core patch manager: named, documented, switchable by the user,
  removed cleanly, refused with a clear message when two mods hook the same function.
- **Saves**: save, list and load by name (menu slots, quick, auto and story saves), side-car data stored next to a
  save.
- **Content**: a mod's `content\` folder overrides game files (read-only opens), later mods in load order win.

The example mod `mods/session_stats` uses every part a typical mod needs. API reference with stability labels:
[docs/API.md](docs/API.md); architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md); how to build, debug,
test and package: [docs/DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md); every game address and hook with its
byte check: [docs/ADDRESSES.md](docs/ADDRESSES.md).

### How the framework works

`SACore.asi` is loaded by Ultimate ASI Loader. It identifies the build by hash, resolves every address of that build
from `src/adapters/` and checks it (prologue bytes or the number of code references), and enables only the
capabilities whose addresses check out. Hooks go through MinHook behind a patch manager that records owner, purpose
and state of every patch (`sacore\diagnostics\patches.txt`). Mods are loaded in dependency order, each behind an
exception guard.

## Tests

| Kind | Command | What it covers |
|---|---|---|
| Unit tests (doctest) | `ctest --test-dir build/x86 -C Release` | config, mod manifests, dependency resolution, patch manager, adapters, save files, build identity, event bus, crash reports, content overrides, scenario parsing |
| Tool tests (unittest) | `python -m pytest -q tests/tools` | installer, mod manager, saves, laa, test runner, packaging, triage |
| Runtime tests | `python tools/run_runtime_tests.py --game <copy> [--game <copy> ...] --scenario tests/runtime/scenarios` | in-game scenarios through the built-in test harness |

Runtime tests start the real game in prepared test copies (never the Steam folder) and drive it through scenario files
(`tests/runtime/scenarios/*.toml`): wait for states, press keys, teleport, load saves, change settings, check the
log, player state, frame rate and saves. Several copies run side by side (two per build cut the quick tier to about 5
minutes); scenarios can share one game start (`session`) or build on each other's saves (`chain`). The default quick
tier runs every scenario except the long ones; `--tier full` adds the soak test, the preload and the whole story in
seven chunks (about 40 minutes). Results and failure logs land in `runtime-results/` and `build/runtime-failures/`.

## Tools

All tools are Python 3.11+ (standard library; `pefile` and `capstone` optional for some), run from the repository
root, and are dry runs unless they get `--apply`.

| Tool | Use |
|---|---|
| `tools/release/installer.py` | install, update, verify, recover, uninstall (shipped as `install.py`) |
| `tools/package_release.py` | build the install package in `dist\` |
| `tools/package_mod.py` | package a built mod as `dist\<id>-<version>.zip` |
| `tools/modman.py` | mod manager: list, enable/disable, profiles, install/uninstall, dependency check |
| `tools/compat_check.py` | does a mod (folder or zip) fit an install: build, core version, API, dependencies |
| `tools/new_mod.py` | create a mod project from `templates/mod` |
| `tools/deploy_dev.py` | copy freshly built core and mods into a test copy |
| `tools/launch.py`, `tools/launch_test.py` | start a copy for development, or start, observe and close it |
| `tools/run_runtime_tests.py` | runtime test runner (see Tests) |
| `tools/saves.py` | inspect, validate, back up and restore saves; copy story saves between installs |
| `tools/laa.py` | set or clear the 4 GB flag in a copy's executable |
| `tools/triage_crash.py` | turn a crash zip into a named call chain and next steps |
| `tools/collect_logs.py` | bundle the diagnostics of one install into a zip |
| `tools/symbolize.py`, `tools/symbols.py` | name `module+0xRVA` from a crash report; archive map and PDB of each build |
| `tools/inventory.py`, `tools/hash_manifest.py` | read-only inventory of an install; SHA-256 manifest of a folder tree |
| `tools/apply_downgrade.py` | apply the Open Downgrader patches to make a 1.0 US copy |
| `tools/install_stack.py` | install the third-party base components into a test copy |
| `tools/find_pattern.py`, `tools/port_address.py` | search code for byte patterns; port a global's address between builds |
| `tools/gen_names_us10.py`, `tools/gen_mission_table.py` | generate the 1.0 US name table and the mission table |
| `tools/common/workshop.py` | game-agnostic helpers: engine and loader detection, PE facts, file manifests |
| `tools/ghidra/` | headless Ghidra scripts for cross-references and decompiles |

## Repository layout

```
src/api/          public mod API headers (sacore_api.h, sacore.hpp, sa_game.hpp)
src/core/         SACore.asi: engine (window, input), game layer, saves, content, stability, patching, mods, test harness
src/adapters/     per-build addresses, byte checks and capabilities
mods/             devtools (console), 22 mods, session_stats (example), testing (test mods, not shipped)
templates/mod/    template for tools/new_mod.py
tests/unit/       unit tests (doctest)
tests/tools/      tool tests (unittest)
tests/runtime/    runtime scenarios for the in-game harness, regression list
tools/            Python tooling (install, packaging, mod manager, saves, tests, diagnostics, analysis)
docs/             documentation
```

## Documentation

| Document | Content |
|---|---|
| [docs/INSTALL.md](docs/INSTALL.md) | play copies, 1.0 US copy, install, verify, configure, update, recover, uninstall |
| [docs/MODS.md](docs/MODS.md) | every mod, its keys and how it is tested |
| [docs/API.md](docs/API.md) | mod API reference with stability labels |
| [docs/DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md) | creating, building, debugging, testing and packaging mods |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | components, start-up, adapters, patching, mod lifecycle |
| [docs/ADDRESSES.md](docs/ADDRESSES.md) | every game address and hook with its byte check, per build |
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | tested builds and components, known problems |
| [docs/RELEASE_NOTES.md](docs/RELEASE_NOTES.md) | changes per version |
| [THIRD_PARTY.md](THIRD_PARTY.md) | third-party components and their licenses |

## Credits

- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (MIT) loads SACore;
  [SilentPatch](https://github.com/CookiePLMonster/SilentPatch) (MIT) fixes many of the game's own bugs.
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause), [toml++](https://github.com/marzer/tomlplusplus)
  (MIT), [Dear ImGui](https://github.com/ocornut/imgui) (MIT) and [doctest](https://github.com/doctest/doctest)
  (MIT) are built in.
- [plugin-sdk](https://github.com/DK22Pac/plugin-sdk) (Zlib) for 1.0 US addresses and names, gta-reversed for
  function knowledge, [FramerateVigilante](https://github.com/JuniorDjjr/FramerateVigilante) (MIT) for the high frame
  rate fixes, reimplemented. The community wikis for the save format and script opcodes.

Full list with versions and licenses: [THIRD_PARTY.md](THIRD_PARTY.md).

## License

[MIT](LICENSE). Grand Theft Auto and San Andreas are trademarks of Take-Two Interactive Software; this project is not
affiliated with them.
