# SACore Mod API (version 1)

Header: `src/api/include/sacore/sacore_api.h` (C ABI). Convenience layer: `src/api/include/sacore/sacore.hpp` (header-only C++20).

## Stability levels

| Label | Meaning |
|---|---|
| stable | Part of API version 1; behaviour and signature do not change within version 1. |
| stable, since 0.8.0 | Appended later in version 1; check `host->struct_size` before use (the C++ wrapper does it). |
| BUILD-SPECIFIC | Exposes game internals; meaning can differ between game builds. Use only together with a build check. |
| EXPERIMENTAL | May change in a later minor version. |

## Rules that apply to every call

- Threading: everything is called on the game thread, and you call the host only from the game thread. Exception: patch detours run on whatever thread the game calls the target on.
- Lifetime: the `SacoreHost` pointer is valid until your `SACORE_EVENT_SHUTDOWN` handler (or `shutdown` callback) returns.
- Strings are UTF-8 and NUL-terminated.
- Errors: functions return 0 / NULL / -1 on failure and log the reason in `sacore\logs\sacore.log`. Nothing throws across the ABI.
- Crash isolation: if your mod crashes inside `sacore_mod_main`, an event handler, a message filter or the shutdown callback, SACore catches it, disables your mod for the session (subscriptions and filters removed) and keeps the game and other mods running. A crash inside your own patch detour is not caught (it runs in the game's call path) and ends in a crash report naming your DLL.
- Versioning: a mod declares `api_version = 1` in `mod.toml`. A mod built for another major version is rejected with a log line; nothing of it is loaded.

## Entry point

```c
int sacore_mod_main(const SacoreHost* host, SacoreModCallbacks* callbacks);  // exported, stable
```

Return 0 to start, any other value to refuse (logged as an error code). Fill `callbacks->shutdown` if you need a final call (alternative: subscribe to `SACORE_EVENT_SHUTDOWN`).

## Host data fields

| Field | Stability | Description |
|---|---|---|
| `struct_size` | stable | Size of the host struct the core passed; fields appended later exist only when it is large enough. |
| `api_version` | stable | `SACORE_API_VERSION` of the core (1). |
| `core_version` | stable | SACore version string, for example `"0.9.2"`. |
| `game_build` | stable | `"newsteam_r2_lv"` or `"us10_hoodlum"`. |
| `mod_id`, `mod_directory` | stable | Your mod's id and its folder (`sacore\mods\<folder>`). |
| `core_internal` | internal | Opaque; never touch. |

## Host functions

| Function | Stability | Description |
|---|---|---|
| `log(host, level, message)` | stable | Writes `[Mods] <mod id>: message` to the core log. Levels trace..error. |
| `capability_state(host, id)` | stable | `SUPPORTED`, `EXPERIMENTAL`, `UNSUPPORTED`, `NOT_INVESTIGATED`. Ids are listed in `sacore\diagnostics\capabilities.txt` (for example `player.read`, `player.money.write`, `save.events`). Check before relying on a feature; the wrapper's `Has()` accepts supported and experimental. |
| `subscribe(host, type, callback, user)` | stable | Returns a subscription id (0 on failure). Removed automatically when the mod stops. |
| `unsubscribe(host, id)` | stable | Safe inside an event handler. |
| `game_state(host)` | stable | 0 start-up ... 5 init, 7 main menu, 8 loading, 9 playing; -1 when unavailable. |
| `get_player(host, out)` | stable | Fills health, armour, money, wanted level, position, interior; returns 0 when there is no player (menu, loading) or `player.read` is unavailable. |
| `set_money(host, money)` | stable | Requires `player.money.write`; returns 0 if unavailable. |
| `config_bool/int/double/string(host, key, ...)` | stable | Values of the settings declared in `mod.toml [config]` (see below). An undeclared key returns the fallback and logs an error. |
| `d3d_device(host)`, `game_window(host)` | stable | `IDirect3DDevice9*` and `HWND`; NULL before device creation. |
| `add_message_filter(host, filter, user)` | stable | Called before the game's window procedure; return non-zero to consume a message. |
| `install_hook(host, info, target, detour, &original)` | stable | Function hook through the core patch manager. The patch id is `<mod id>.<info.id>`, shown in `patches.txt` and switchable via `core.toml [patches] disabled`. Returns 0 when refused (another patch already hooks the target, or switched off). |
| `symbol_address(host, name)` | BUILD-SPECIFIC | Runtime address of an adapter symbol (`docs/ADDRESSES.md`), 0 if unknown or its signature failed. |
| `diagnostics_text(host, kind, buffer, size)` | EXPERIMENTAL | Text for developer tools: `mods`, `capabilities`, `patches`, `log`. |
| `savedata_set(host, key, value)` | stable, since 0.8.0 | Stores a string with the save the player makes next (`sacore\savedata\slot<N>\<mod id>.toml`). |
| `savedata_get(host, key, buffer, size)` | stable, since 0.8.0 | Value restored from the loaded save; `(size_t)-1` when not set. |
| `save_slot(host)` | stable, since 0.8.0 | Slot 1..8 of the last save or load in this session, 0 if none. |
| `save_game(host, name)` | stable, since 1.1.0 | Saves through the game's routine from the message loop: `slot1`..`slot8` (menu slots), `quick1`..`quick9` / `auto1`..`auto9` (own files `GTASAqsN.b` / `GTASAasN.b`, never touching the menu slots; capability `save.extra_slots`) or, since 1.3.0, `story1`..`story120` (`GTASAstN.b`, the story runner's story points; not in `save_list`). Returns 1 when scheduled; result as `SACORE_EVENT_SAVE_COMPLETED`. Refused when not playing, in an interior or cutscene, while a mission or mini game runs (such saves crash on load), or another save is pending. |
| `save_list(host, buffer, size)` | stable, since 1.1.0 | Every existing save, one line each, tab-separated: name, file, last write time (Unix seconds), valid (1/0). |
| `settings_list(host, buffer, size)` | stable, since 1.0.0 | Every running mod's declared settings, one line each, tab-separated: mod id, key, type (`bool`/`int`/`double`/`string`), value, min, max, allowed values (`|`), description. Returns the full length. |
| `settings_set(host, mod_id, key, value)` | stable, since 1.0.0 | Validates the text value against that mod's `[config]` declaration (type, range, allowed values), writes `sacore\config\mods\<id>.toml` and sends that mod `SACORE_EVENT_CONFIG_CHANGED`. Returns 1, or 0 with the reason in the log. Used by the F11 console's Settings page. |

## Events

| Type | Fields | When |
|---|---|---|
| `SACORE_EVENT_FRAME` | `frame`, `device` | every presented frame (before Present) |
| `SACORE_EVENT_GAME_STATE_CHANGED` | `state_from`, `state_to` | main loop state changes (sampled twice per second) |
| `SACORE_EVENT_SAVE_STARTED` / `_COMPLETED` | `success` | around every save (menu or SACore) |
| `SACORE_EVENT_LOAD_STARTED` / `_COMPLETED` | `success` | around every load; `success = 0` also when nothing was loaded |
| `SACORE_EVENT_DEVICE_CREATED` | `device`, `window` | once |
| `SACORE_EVENT_DEVICE_RESET` | `device`, `before_reset`, `success` | release `D3DPOOL_DEFAULT` resources when `before_reset = 1`, recreate after |
| `SACORE_EVENT_CONFIG_CHANGED` | `text` (the key; since 1.0.0, check `struct_size`) | one of this mod's settings was changed while the game runs; read the new value with `config_*` |
| `SACORE_EVENT_SHUTDOWN` | | the game window is closing; last event |

## mod.toml

```toml
id = "my_mod"                  # [a-z0-9_.-], 1..64, unique
name = "My Mod"
version = "1.0.0"
author = "you"
description = "What it does"
api_version = 1
min_core_version = "0.8.0"     # optional
dll = "my_mod.dll"             # optional: without it the mod is content-only
target_builds = ["newsteam_r2_lv", "us10_hoodlum"]   # optional, default: every supported build
load_after = ["other"]         # optional ordering
load_before = ["another"]
[dependencies]                 # required mods with version requirements
core_utils = "^1.2"            # *, =, >, >=, <, <=, ^, ~ joined with commas
[optional_dependencies]
widescreen = ">=2.0"
[features]
experimental_hud = false
[config.speed]                 # typed setting: default decides the type (bool, integer, number, string)
default = 2.5
min = 0.5
max = 10
description = "Shown as a comment in sacore\config\mods\my_mod.toml"
[config.hud.style]             # nested names become "hud.style"
default = "classic"
allowed = ["classic", "minimal"]
```

## Content

Files in `<mod folder>\content\` replace game files with the same relative path (`content\data\handling.cfg` replaces `data\handling.cfg`) for reading. The later mod in load order wins a shared file; conflicts are logged. Whole files only: single entries inside IMG archives cannot be replaced.

## C++ wrapper (sacore.hpp)

```cpp
#include <sacore/sacore.hpp>
std::unique_ptr<sacore::mod::Host> g_host;   // keep alive: handlers live inside it

extern "C" __declspec(dllexport) int sacore_mod_main(const SacoreHost* raw, SacoreModCallbacks*) {
    g_host = std::make_unique<sacore::mod::Host>(raw);
    g_host->Info("started on {}", g_host->GameBuild());
    if (g_host->Has("player.read")) {
        g_host->On(SACORE_EVENT_FRAME, [](const SacoreEvent&) {
            if (auto player = g_host->Player()) { /* ... */ }
        });
    }
    return 0;
}
```

| Wrapper | C function |
|---|---|
| `Id()`, `GameBuild()`, `CoreVersion()` | host data fields |
| `Raw()` | the `SacoreHost*` itself, for calls without a wrapper (`d3d_device`, `game_window`, `add_message_filter`, `symbol_address`) |
| `Info/Warn/Error(fmt, args...)`, `Log(level, text)` | `log` |
| `Capability(id)`, `Has(id)` | `capability_state` |
| `On(type, std::function)`, `Off(id)` | `subscribe`, `unsubscribe` |
| `GameState()`, `Player()`, `SetMoney()` | `game_state`, `get_player`, `set_money` |
| `ConfigBool/Int/Double/String(key)` | `config_*` |
| `Hook(id, target, reason, function, detour, &original)` | `install_hook` |
| `SetSaveValue(key, value)`, `SaveValue(key)`, `SaveSlot()`, `HasSaveData()` | `savedata_*`, `save_slot` (checks `struct_size`) |
| `SaveGame(name)`, `SaveList()`, `HasSaveGame()` | `save_game`, `save_list` (checks `struct_size`) |
| `OnKey(vk, callback)`, `ClearKeys()` | `add_message_filter` (key down, no repeat, key still reaches the game) |
| `Symbol(name)`, `GameFunction<Fn>(name)`, `GameVariable<T>(name)` | `symbol_address` (BUILD-SPECIFIC) |
| `SettingsList()`, `SetSetting(mod, key, value)`, `HasLiveSettings()` | `settings_list`, `settings_set` (checks `struct_size`) |
| `Diagnostics(kind)` | `diagnostics_text` |

Reference mod using all of it: `mods/session_stats/`.
