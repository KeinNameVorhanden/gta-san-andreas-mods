# Developer Guide

How to create, build, test and ship a SACore mod. API details: `docs/API.md`. Architecture: `docs/ARCHITECTURE.md`.

## 1. Requirements

- Windows, Visual Studio 2026 (or 2022) with the C++ x86 toolset, CMake 3.25+, Python 3.11+, git.
- A test copy of the game with SACore installed (see `docs/INSTALL.md`); never develop against the Steam folder.

## 2. Build the framework

```
cmake --preset x86
cmake --build --preset x86-release
ctest --test-dir build/x86 -C Release
python -m unittest discover -s tests/tools
```

Outputs: `build/x86/src/core/Release/SACore.asi` (+ `.pdb`, `.map`), mods under `build/x86/mods/<name>/Release/`.

## 3. Create a mod

```
python tools/new_mod.py my_mod --name "My Mod" --author you
cmake --preset x86
cmake --build --preset x86-release --target my_mod
```

This creates `mods/my_mod/` (manifest, CMake project, C++ entry point using `sacore.hpp`, README). Every folder under `mods/` with a `CMakeLists.txt` is built automatically.

## 4. Subscribe to events, read state

```cpp
g_host->On(SACORE_EVENT_GAME_STATE_CHANGED, [](const SacoreEvent& e) {
    if (e.state_to == 9) { g_host->Info("playing"); }
});
if (g_host->Has("player.read")) {
    if (auto p = g_host->Player()) { g_host->Info("money {}", p->money); }
}
```

Always check capabilities: `sacore\diagnostics\capabilities.txt` lists them per build with their state.

Gameplay mods: `#include <sacore/sa_game.hpp>` gives mission, cutscene, scripted-scene, wanted and vehicle state plus the game's help box (`ShowHelp`), each checked against its capability. World changes belong in `SACORE_EVENT_GAME_TICK`, hotkeys in `OnKey(vk, callback)`.

## 5. Patch the game (only when there is no event for it)

Use `Hook(...)` (C: `install_hook`) so the patch is registered, documented in `patches.txt`, switchable in `core.toml` and refused if another patch already owns the target. For game functions use `symbol_address` (BUILD-SPECIFIC) and check that it is not 0. Keep detours short and never throw from them.

## 6. Settings, save data, content

- Settings: declare in `mod.toml [config]`; the user edits `sacore\config\mods\<id>.toml`; read with `ConfigBool/Int/Double/String`.
- Save data: `SetSaveValue` before or during `SACORE_EVENT_SAVE_STARTED`; read with `SaveValue` on `SACORE_EVENT_LOAD_COMPLETED`.
- Content: put replacement files in `mods/<name>/content/<game path>`; a mod can be content-only (no `dll`).

## 7. Deploy and run

```
python tools/deploy_dev.py --game D:\Games\GTASA-Test\steam30 --mod my_mod            # dry-run
python tools/deploy_dev.py --game D:\Games\GTASA-Test\steam30 --mod my_mod --apply
python tools/launch.py --game D:\Games\GTASA-Test\steam30
```

Log: `<game>\sacore\logs\sacore.log` (previous runs: `sacore.1.log` ... `sacore.5.log`). The in-game console (devtools mod, F11) shows mods, capabilities, patches, the log and crash reports.

## 8. Test

- Unit tests (`tests/unit`, doctest): pure logic, no game. Test real behaviour, not constants.
- Runtime scenarios (`tests/runtime/scenarios/*.toml`): run inside the game through the harness and write `results.json`:

```
python tools/run_runtime_tests.py --game D:\Games\GTASA-Test\us10 --game D:\Games\GTASA-Test\us10b --game D:\Games\GTASA-Test\steam30 --game D:\Games\GTASA-Test\steam30b --scenario tests/runtime/scenarios
```

  The quick tier (default) takes about 4.5 minutes for both builds on four copies (2 per build); `--tier full` adds
  the soak test and the whole-story chain (`--serial` runs one game at a time, `--no-sessions` starts the game for
  every scenario). How it stays fast:
  - All copies run side by side. The runner holds the game's instance name ("Grand theft auto San Andreas") with an
    empty access list, so every copy passes the single-instance check; copies of the same build (same exe) share one
    job queue, longest scenario first (durations of the last run in `build/runtime-durations.json`).
  - The game's stream semaphore is named ("CdStream"), so two copies of one build would steal each other's read
    requests and freeze while loading. With a scenario armed, SACore gives it a private name, or (1.0 US, where the
    game creates it before SACore loads) replaces the idle stream thread with one on a private semaphore
    (capability `testing.parallel_copies`).
  - Scenarios with the same `session` (ordered by `session_order`) run in one game start; a `chain` (ordered by
    `chain_order`, for scenarios that load each other's saves) runs as one job on one copy; `tier = "full"` keeps a
    scenario out of the quick tier.
  - Waits are conditions, not seconds: `wait_player` with `field = "cutscene"` or `"mission"` (0/1), `expect_log`.
  - A failed run keeps its game log in `build/runtime-failures/<copy>-<scenario>.log`.
  Second copies are plain copies of the test copies (never of the Steam folder): `robocopy <copy> <copy>b /E`, then
  deploy to them like to the first ones.

  Steps: `wait_state`, `wait_seconds`, `wait_player`, `expect_player`, `expect_capability`, `expect_patch`, `expect_log`, `expect_window_caption`, `expect_frames_advance`, `expect_save_valid`, `set_money`, `save_slot` (`value` = menu slot or `name` = `quick1`), `load_save` (`name`), `start_new_game` (pause-menu new game), `press_key` (`value` = Windows key code), `teleport` (`x`, `y`, `z`: the player, like SET_CHAR_COORDINATES, run in the next game tick), `set_setting` (`name` = mod, `id` = key, `contains` = value), `simulate_focus_loss`, `simulate_menu_key`, `crash_now`, `hold_key`, `mark_position`, `expect_distance`, `expect_ratio`, `set_waypoint`, `set_health`, `import_save`, `quit`; `expect_log` also takes `fail_contains`. A scenario can carry `[config]` and `[mod_config.<id>]` overrides (restored afterwards) and `expect_log_after_exit`.
- Test copies need `[testing] auto_start_new_game = true` so scenarios reach gameplay without input.

## 9. Debug a crash

1. Every crash leaves `<game>\sacore\crashes\<time>.zip`: report, minidump, the session log, `core.toml`, mod settings and the capability/patch lists. Nothing is shown to the player.
2. `python tools/triage_crash.py <zip>` prints the call chain with names (SACore and mods from the archived map of exactly that binary, 1.0 US game functions from plugin-sdk names, newsteam from SACore's own symbols), the code at the fault, the minidump threads and the log tail, plus a Ghidra command for unnamed game functions.
3. The report's stack scan lists call-verified return addresses; it keeps going where the frame-pointer walk stops (most game code).
4. `tools/deploy_dev.py` and `tools/package_release.py` archive `.map`/`.pdb` of every deployed binary under `symbols/<sha8>/`; open the `.dmp` in Visual Studio with that `.pdb`.
5. `python tools/collect_logs.py --game <install> --out <folder>` bundles everything else (no saves, no personal files).

## 10. Package and install

```
python tools/package_mod.py my_mod                          # dist/my_mod-<version>.zip
python tools/compat_check.py --game <install> dist/my_mod-<version>.zip
python tools/modman.py --game <install> --apply install dist/my_mod-<version>.zip
```

The mod manager also enables/disables mods, ASI plugins, CLEO scripts and Modloader folders, keeps profiles and backs up before every change.
