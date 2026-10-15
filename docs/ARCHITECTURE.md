# Architecture

State: SACore 1.0 (written from the implementation).

## 1. Understanding

Goal: a stable GTA San Andreas first, then QoL mods, on two builds: `newsteam_r2_lv` (the Steam exe) and `us10_hoodlum` (downgraded 1.0 US copy). C++20, 32-bit x86, MSVC. License: MIT.

## 2. Findings that shaped the design

| Finding | Consequence |
|---|---|
| newsteam is relocated by ASLR (base 0x009F0000 observed); 1.0 US is not | addresses are RVAs resolved against the runtime base |
| Community addresses exist almost only for 1.0 US | newsteam addresses come from own Ghidra work; every address is checked by a byte signature at runtime |
| The ASI loader loads plugins after the game's earliest startup code (User Files lookup, DirectInput, window creation) | a small set of import-table hooks is installed in `DllMain` and decides lazily (early hooks); everything else starts at the first `Direct3DCreate9` |
| SilentPatch fixes most known crashes | reused, not duplicated; our hooks chain with it |
| Compatibility shims or plugins can route calls around inline hooks (Device Selection dialog) | window-level CBT hook and import-table hooks where inline hooks proved unreliable |
| Modloader does not work on either build in this setup | own loose-file content layer |
| The game saves through a member function with `ret 4` | call conventions are checked from the epilogue before calling game code |

## 3. Reused components

| Component | Version | Role |
|---|---|---|
| Ultimate ASI Loader | 9.7.4 (`dinput8.dll`) | loads `scripts\*.asi` |
| SilentPatch SA | 34.1 | crash and bug fixes |
| MinHook | 1.3.4 | function hooks (behind the patch manager) |
| toml++ | 3.4.0 | manifests, configs, side-car data |
| Dear ImGui | 1.92.9b | developer console |
| doctest | 2.5.3 | unit tests |
| plugin-sdk, gta-reversed | reference only | 1.0 US addresses and layouts; no code copied |

## 4. Components

```
dinput8.dll (Ultimate ASI Loader)
  scripts\SilentPatchSA.asi
  scripts\SACore.asi
    early hooks (DllMain): RegQueryValueExA "Personal", SetForegroundWindow, SetFocus
    CoreRuntime (first Direct3DCreate9, game thread)
      Logger (+ ring buffer) -> sacore\logs\sacore.log
      Config (core.toml, typed, validated, migrated)
      PatchManager (MinHook hooks, import hooks, memory writes; registered, switchable, reversible)
      Engine        D3D9 CreateDevice/Present/Reset, window (windowed at game resolution, style/geometry guards, background, mouse guards), audio
      Stability     crash filter + minidump + report with stack scan + crash package zip, Device Selection auto-confirm, message box log
      GameAdapter   per-build symbols with signature checks -> capability states
      Game          state, player, money, intro skip, save/load events, exit crash guard, test automation (menu routine)
      SaveService   separate User Files, backups, validation, slot tracking, side-car data
      Harness       runtime scenarios (only with sacore\testing\scenario.toml)
      ModLoader     discovery, resolution, lifecycle, SEH isolation, per-mod config
      ModApi        C ABI for mods
      ContentService loose-file overrides (CreateFileA import)
  sacore\mods\<id>\  mods (DLL and/or content)
```

| Source | Responsibility |
|---|---|
| `src/core/runtime/` | bootstrap (`dllmain.cpp`), early hooks, `CoreRuntime` wiring, version resource |
| `src/core/engine/` | build-independent D3D9 / window / input / audio behaviour |
| `src/core/game/`, `src/adapters/` | module image, signature checks, capability model, game state access |
| `src/core/stability/` | crash reports and stability hooks |
| `src/core/saves/` | save file checks, backups, save service |
| `src/core/content/` | content registry and file redirection |
| `src/core/testing/` | scenario parser and harness |
| `src/core/*.cpp` | logger, config, versions, manifests, dependency resolver, event bus, patch manager, mod loader, mod API |
| `src/api/include/sacore/` | public C ABI and C++ wrapper |

## 5. Bootstrap and threading

1. UAL loads `SACore.asi`. `DllMain` installs the early import hooks and a MinHook hook on `Direct3DCreate9`; nothing else.
2. First `Direct3DCreate9` on the game thread: log, config, build identification (SHA-256 of the exe). Unknown build: everything stays inactive (only pass-through hooks remain).
3. Known build: stability layer, adapter and capability report, engine, game layer (intro skip before the main loop runs), saves, harness, mods, content.
4. All game interaction happens on the game thread; events are dispatched synchronously; the only other threads are the game's own. Harness saves run from the message loop, never inside Present.
5. Shutdown: on `WM_DESTROY` of the game window: patch report, `SACORE_EVENT_SHUTDOWN`, mod shutdown callbacks in reverse order. At process exit nothing is torn down.

## 6. Adapters and capabilities

- Symbol kinds: `CodeReferences` (global referenced N times), `Prologue` (function start bytes; a leading `E9` means another plugin hooked it and is accepted), `Instruction` (an exact instruction containing the address).
- A capability lists required symbols; any failed symbol makes it Unsupported with the reason; otherwise Supported if runtime-verified on that build (flag in the adapter data, evidence in `docs/COMPATIBILITY.md`), else Experimental. Engine capabilities need no symbols.
- Report: `sacore\diagnostics\capabilities.txt` and the log.

## 7. Patching

Every hook or write has an id, owner, target, reason, expected behaviour and failure behaviour; ids are listed in `sacore\diagnostics\patches.txt` and can be switched off in `core.toml [patches] disabled`. A second hook on the same target is refused as a conflict. Hooks are removable; memory writes are restored unless they set game state.

## 8. Mods

Discovery (`sacore\mods\<folder>\mod.toml`), validation, dependency resolution (versions, cycles, API version, core version, target builds, load order), loading, `sacore_mod_main` under an SEH guard, running, shutdown. A crash in any mod callback disables that mod only. Per-mod config declared in the manifest. Content-only mods have no DLL.

## 9. Testing

| Level | Where | What |
|---|---|---|
| unit | `tests/unit` (doctest, 53 cases) | resolver, versions, manifests, config, logger, event bus (incl. slow-handler attribution), adapter checks, crash report, patch manager (real hooks), scenarios, save checks, content registry |
| tools | `tests/tools` (unittest, 29 tests) | mod manager, saves tool, generator, installer, workshop utilities, crash triage |
| runtime | `tests/runtime/scenarios` via `tools/run_runtime_tests.py` | smoke, crash, patches_off, mod_api, devtools, saves, content, example_mod, config_values, incomplete_shutdown, new_game_restart, install_check, soak; both builds |
| manual | manual check list (`docs/COMPATIBILITY.md`) | visuals, feel, real menu save/load, long play |

## 10. Risks and their handling

| Risk | Handling |
|---|---|
| Address drift between builds | RVA + signature per symbol, capability disabled on mismatch |
| Hook conflicts with SilentPatch | chain on existing JMP, runtime tests with SilentPatch always installed |
| Calling game code with a wrong convention | epilogue checked (`ret`/`ret N`) before declaring function types |
| Re-entrancy (game presents frames inside its own routines) | harness guard, saves from the message loop |
| Shared User Files between builds | separate folder per install |
| Background running grabbing the mouse | mouse guards while not in front |
| Crash inside a mod | SEH guards; mod disabled, report written |
| Crash anywhere | one zip per crash, `tools/triage_crash.py` names the call chain from the exact binary's map |
| Game bugs at exit | WinMain's final RenderWare teardown left to the process exit (otherwise plugin destructors run on freed data) |
| Multiplayer anti-cheat | test copies are never connected to SA-MP, open.mp or MTA |
