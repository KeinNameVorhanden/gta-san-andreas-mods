# Address Table

Every game address the project uses. RVA = offset from the runtime module base (newsteam is relocated by ASLR, 1.0 US is not; 1.0 US VA = RVA + 0x400000). Verification names the test type. Runtime code checks: each address must appear as an absolute operand in `.text` at least N times (`probe::CountCodeReferences`), `gGameState` additionally needs the exact `mov [gGameState], 2` instruction.

## Variables

| Name | Type | newsteam_r2_lv RVA | us10_hoodlum RVA (VA) | Source | Verification |
|---|---|---|---|---|---|
| `gGameState` | `int` | `0x919020` | `0x88D4C0` (`0xC8D4C0`) | newsteam: own Ghidra decompile of WinMain (`FUN_0077cd60`, switch on `DAT_00d19020`, case 1 plays `movies\Logo.mpg`); 1.0 US: plugin-sdk `CGame.cpp`, same switch in own decompile (`FUN_00748710`) | runtime, both builds: 19 / 23 code refs, signature found, intro skip works |
| `FrontEndMenuManager` (`CMenuManager`) | struct | `0x833938` | `0x7A6748` (`0xBA6748`) | WinMain frontend case reads `+0x5C` and `+0x60` on both builds (own decompiles); 1.0 US base matches plugin-sdk | runtime, both builds: setting `+0x60` in state 7 starts a new game (P2) |
| `FrontEndMenuManager+0x5C` | `bool` | | | named `m_bMenuActive` by plugin-sdk / gta-reversed | PROBABLE: reads 0 then 1 in the frontend and stays 1 after P2 while gameplay runs; meaning not fully confirmed |
| `FrontEndMenuManager+0x60` | `bool` | | | start-game-loading flag (WinMain case 7 moves to state 8 when set) | CONFIRMED by runtime test (P2) |
| `CWorld::Players` (`CPlayerInfo[]`, stride `0x190`) | struct array | `0x8100D0` | `0x77CD98` (`0xB7CD98`) | instruction pattern `imul eax,eax,190h; mov eax,[eax+disp32]`: 40 of 47 hits on newsteam, 42 of 45 on 1.0 US (`tools/find_pattern.py`); 1.0 US matches plugin-sdk | runtime, both builds: ped pointer valid, money 0 at new game |
| `CGame::currArea` | `int` | `0x7FFBE4` | `0x772914` (`0xB72914`) | 1.0 US: plugin-sdk / own decompile (`CGame` reinit writes it); newsteam: `tools/port_address.py` with 4-byte context, 7 unique-context votes, 151 code refs vs 136 | runtime, both builds: 0 -> 14 during the intro cutscene at the same moment |
| `ForegroundApp` | `bool` | `0x54DB20` | `0x4D621C` (`0x8D621C`) | newsteam window procedure `FUN_0077c120`: `WM_ACTIVATE` (inactive) and `WM_KILLFOCUS` clear it, `WM_SETFOCUS` sets it; WinMain sleeps while it is 0 | read-only diagnostic on 1.0 US; not written by our code |

## Functions

| Name | Signature | newsteam_r2_lv RVA | us10_hoodlum RVA (VA) | Prologue signature | Source | Verification |
|---|---|---|---|---|---|---|
| `CMenuManager::DoSettingsBeforeStartingAGame` | `void __thiscall (CMenuManager*)` | `0x186E90` | `0x173330` (`0x573330`) | newsteam `33 C0 56 8B F1 A3`; 1.0 US `56 8B F1 57 33 C0 B9 0E 00 00 00` | 1.0 US: plugin-sdk; newsteam: byte search for its unique body (`+0x32 = 1` then `+0x5D = 1`), same pad clearing and calls | runtime, both builds: auto start and in-game restart (R19) |
| `RsEventHandler` | `int __cdecl (int event, void*)` | `0x237900` | `0x219B60` (`0x619B60`) | newsteam `55 8B EC 56 8B 75 0C 57 8B 7D 08 56`; 1.0 US `56 8B 74 24 0C 57 8B 7C 24 0C 56 57` | 1.0 US: plugin-sdk; newsteam: WinMain calls with events 0x16/0x19 (own decompile) | runtime, both builds: exit guard (R20) |
| WinMain exit `push 0x16; call RsEventHandler` | call site | `0x37D557` | `0x348E80` | `6A 16 E8` | own decompiles of both WinMains (after the main loop, before `DestroyWindow`); the only other sender of 0x16 is a device change path | runtime, both builds |
| `CGenericGameStorage::ms_ValidSaveName` | `char[256]` filled by `MakeValidSaveName` | `0x8A3B50` | `0x816DB8` (`0xC16DB8`) | instruction `B8` + address (`mov eax, buffer`) | 1.0 US: plugin-sdk; newsteam: same function body (format, copy loop, `mov eax, buffer`), own disassembly | runtime, both builds: quick1 written as GTASAqs1.b |
| `FrontEndMenuManager` | `CMenuManager` global | `0x833938` | `0x7A6748` | 36 / 29 code references | as before | as before |
| `CGenericGameStorage::GenericSave` | `bool __cdecl (int)` | `0x1ECD50` | `0x1D13E0` (`0x5D13E0`) | newsteam `55 8B EC 81 EC 38 01 00 00`; 1.0 US `81 EC 38 01 00 00` | 1.0 US: plugin-sdk; newsteam: only function with 4 references to the `"BLOCK"` string (same as 1.0 US), same body shape (flag clear, slot check, 0xC800 buffer loop), one caller | hook installs on both builds; firing on a real save through the menu: manual check |
| `CGenericGameStorage::GenericLoad` | `bool __cdecl (bool*)` | `0x1ED120` | `0x1D17B0` (`0x5D17B0`) | newsteam `55 8B EC 8B 45 08 81 EC 7C 02 00 00`; 1.0 US `8B 44 24 04 81 EC 7C 02 00 00` | 1.0 US: plugin-sdk; newsteam: only function with 1 reference to `"BLOCK"`, same stack frame size 0x27C and `*out = 0` start | runtime, both builds: fires during new-game init (returns false: nothing to load) |

| `CCutsceneMgr::IsCutsceneSkipButtonBeingPressed` | `bool __cdecl ()` | `0xDFE40` | `0x0D5D10` (`0x4D5D10`) | newsteam `6A 00 E8 ?? ?? ?? ?? 83 C4 04 66 83 78 20 00`; 1.0 US `6A 00 E8 ?? ?? ?? ?? 83 C4 04 E9` | first call in `CRunningScript::Process`, plugin-sdk name | runtime, both builds: cutscene_skip ends the intro cutscene (auto and Enter) |
| `ScriptDebugKeyPressed` (stub behind IS_PS2_KEYBOARD_KEY_PRESSED 0x0735) | `bool __thiscall (obj, int key, int, const char*)`, `ret 0Ch` | `0x167B30` | `0x171980` (`0x571980`) | `32 C0 C2 0C 00` | single caller in the 0x0735 case of `ProcessCommands1800To1899`, `esi` = CRunningScript at the call | runtime, both builds: mission_pass passes Big Smoke through Rockstar's debug branch |
| `ScriptDebugKeyJustPressed` (stub behind 0x0736) | same | `0x167B20` | `0x171970` (`0x571970`) | `32 C0 C2 0C 00` | single caller in the 0x0736 case | hook installs on both builds |

## Struct offsets (identical on both builds where verified)

| Field | Offset | Source | Verification |
|---|---|---|---|
| `CPlayerInfo::m_pPed` | `+0x00` | plugin-sdk | runtime, both builds |
| `CPlayerInfo::m_nMoney` / `m_nDisplayMoney` | `+0xB8` / `+0xBC` | plugin-sdk; both offsets appear in the Players pattern hits of both builds | runtime: 0 at new game |
| `CEntity::m_placement.pos` | `+0x04` | plugin-sdk | fallback only |
| `CEntity::m_matrix` (`CMatrixLink*`), pos at `+0x30` | `+0x14` | plugin-sdk | runtime, both builds: (2488.6, -1666.9, 13.9) at new game start (Grove Street) |
| `CEntity::m_nAreaCode` | `+0x2F` | plugin-sdk | runtime: 0 outdoors |
| `CPed::m_pPlayerData` -> `CPlayerData+0` = `CWanted*` | `+0x480` | plugin-sdk | runtime: chain readable, level 0 |
| `CWanted::m_nWantedLevel` | `+0x2C` | plugin-sdk | runtime: 0 (change not yet observed) |
| `CPed::m_fHealth` / `m_fArmour` | `+0x540` / `+0x548` | plugin-sdk | runtime: 100 / 0 at new game (change not yet observed) |

Game state values (WinMain loop, both builds): 0 start-up, 1 init logo movie, 2 logo movie, 3 init intro movie, 4 intro movie, 5 init once, 6 init frontend, 7 frontend, 8 init playing game, 9 playing game.

## Patches

| Id | Target | Reason | Expected behavior | Dependencies | Compatibility | Failure behavior | Disable |
|---|---|---|---|---|---|---|---|
| P1 | write `gGameState` 0 -> 5 at the first `Direct3DCreate9` call | DirectShow intro movies never finish in the test environment (state stays 2 for 100+ s, observed on us10) | game skips logo and intro movies and goes straight to init-once and the main menu | `gGameState` signature | both builds; only when the value is still 0 | signature missing or value not 0: log line, no write | `SkipIntroMovies=0` |
| P2 | write `FrontEndMenuManager+0x60` = 1 after 6 samples (3 s) in state 7 | test automation: reach gameplay without input | new game starts (state 8 -> 9, player at Grove Street) | menu manager signature | both builds | address check fails: no write | `AutoStartNewGame=0` (default) |
| H6 | MinHook on `GenericSave` | SaveStarted / SaveCompleted events | logs money at save start, result and duration; calls the original unchanged | prologue signature; a target that already starts with `E9` is chained and logged | both builds | signature mismatch: hook not installed, log line | not configurable in the probe (removed with the probe) |
| H7 | MinHook on `GenericLoad` | LoadStarted / LoadCompleted events | logs result and duration; calls the original unchanged | prologue signature | both builds | as H6 | as H6 |
| W1 | write `CPlayerInfo::m_nMoney` += N once on the first sample in state 9 with a valid ped | safe-write test | money increases by N, HUD follows (`m_nDisplayMoney` catches up) | Players code-reference check | both builds; clamped to 0..999,999,999 | no ped or unreadable: no write | `GiveMoneyOnStart=0` (default) |

## Build-independent hooks and options (no game addresses)

| Id | Patch id | Target | Purpose | Setting (`sacore\config\core.toml`) |
|---|---|---|---|---|
| H1 | (bootstrap) | `d3d9.dll!Direct3DCreate9` (MinHook inline) | earliest game-thread entry point for initialization | always |
| H2 | `engine.create_device` | `IDirect3D9::CreateDevice` (vtable slot 16) | windowed mode: force `Windowed=TRUE`, size the window to the game resolution | `[window] windowed`, `client_width` (0 = game resolution) |
| H3 | `engine.present` | `IDirect3DDevice9::Present` (slot 17) | frame events, state sampling, safety net for the window frame | always |
| H4 | `engine.reset` | `IDirect3DDevice9::Reset` (slot 16) | keep windowed mode on device resets; the window follows a resolution change | with windowed |
| H5 | (subclass) | game window procedure | run in background: `WM_ACTIVATE(WA_INACTIVE)` and `WM_KILLFOCUS` go to `DefWindowProc`, `WM_ACTIVATEAPP(FALSE)` passed as TRUE, cursor clip released; logs minimize/close requests | `[window] run_in_background` (windowed only) |
| H6 | `engine.mouse_guard_clip`, `_position`, `_capture` | game imports `user32!ClipCursor`, `SetCursorPos`, `SetCapture` | skipped while the game window is not in front (run in background keeps the game's per-frame cursor handling alive) | with run in background |
| H7 | `engine.window_style_guard` | game import `user32!SetWindowLongA` | the game's `WS_POPUP` switch after device creation becomes SACore's windowed style (R17) | with windowed |
| H8 | `engine.window_geometry_guard` | game import `user32!SetWindowPos` | the game's moves/resizes of its window keep only z-order and show flags (R17) | with windowed |
| O1 | (none) | Core Audio `ISimpleAudioVolume` of the process session | start muted (unmute in the Windows volume mixer) | `[audio] start_muted` |

## Stability hooks (build independent)

| Id | Target | Purpose | Setting (`sacore\config\core.toml`) |
|---|---|---|---|
| crash filter (not a patch) | `SetUnhandledExceptionFilter` at start, re-asserted on the first frame and every 30 s | keep the SACore crash filter first; a filter set later by the game or plugins is chained after it | `[stability] crash_dumps` |
| CBT hook (not a patch) | `SetWindowsHookEx(WH_CBT)` on the game thread | confirm the "Device Selection" dialog with its default device | `[stability] skip_device_selection` |
| (same hook, a selection) | the dialog's video mode list is combo box 1001 on both builds (1.0 dialog procedure 0x745E50, newsteam 0x779D47 area); on OK the game takes `CB_GETCURSEL`, then `CB_GETITEMDATA` as the RenderWare mode (1.0 `GcurSelVM` 0x8D6220, newsteam 0xD1DC54). Entries read like `1280 x 720 x 32 (WIDESCREEN)` | choose the resolution before OK | `[stability] device_selection_resolution` |
| `stability.log_message_boxes` | game import table entry `user32!MessageBoxA` | log the game's error message boxes | always |

Finding: neither an inline hook on `user32!DialogBoxParamA` nor the game's import entry saw the Device Selection dialog on newsteam with SilentPatch installed, although `psSelectDevice` (`FUN_0077a110`) is the only caller in the exe; the window-level CBT hook works on both builds.

## Save system

| Name | Signature | newsteam_r2_lv RVA | us10_hoodlum RVA (VA) | Prologue | Source | Verification |
|---|---|---|---|---|---|---|
| `CGenericGameStorage::MakeValidSaveName` | `void __cdecl (int slotIndex)` (0-based; file `GTASAsf<slot+1>.b`) | `0x1EC7D0` | `0x1D0E90` (`0x5D0E90`) | newsteam `55 8B EC 81 EC 04 01 00 00`; 1.0 US `81 EC 04 01 00 00` | 1.0 US: plugin-sdk; newsteam: first call in the save-slot routine, same 0x104 frame | runtime, both builds: slot 8 tracked |
| `C_PcSave::SaveSlot` | `char __stdcall (char slotIndex)`, returns 0 on success, 2 on failure; member function ending in `ret 4`, body ignores `this` | `0x236D40` | `0x219060` (`0x619060`) | newsteam `55 8B EC 0F BE 45 08 50 E8`; 1.0 US `0F BE 44 24 04 50 E8` | only caller of `GenericSave` on both builds (own decompiles) | runtime, both builds: valid saves written; calling it as `__cdecl` shifted the caller's stack by 4 bytes (crash, fixed) |
| `C_PcSave::SetSaveDirectory` | `void __cdecl (const char* folder)`, writes "<folder>\\GTASAsf" into the save path prefix | `0x236D20` | `0x219040` (`0x619040`) | newsteam `55 8B EC 8B 45 08 68`; 1.0 US `8B 44 24 04 68` | only writer of the prefix (`DAT_00ca3c98` / `0xC16F18`), called once at startup before plugins load | runtime, both builds: saves land in the separate folder |
| User Files lookup | `RegQueryValueExA(HKCU\...\Shell Folders, "Personal")` in the game's user-folder routine (`FUN_00778e90` / 1.0 US `0x744FB0`) | | | | own decompile | first lookup happens before plugins load (0 queries seen at core init); later lookups (settings file, save) go through the import hook |

Findings:
- `FrontEndMenuManager+0x60` is the load-saved-game flag: state 8 initialisation (1.0 US `FUN_0053c680`) calls `GenericLoad` when it is set, retries 50 frames with "load failed" messages and then starts a new game. The test trigger now writes `gGameState = 8` instead; a new game starts without any load attempt.
- Save version id: newsteam writes `0xFD148DF6` (the documented "2.00" id), 1.0 US writes `0x35DA8175`. A shared User Files folder lets one build overwrite or try to load the other's saves (observed: the first newsteam backup of slot 8 was a 1.0 US save).

| Id | Target | Purpose | Setting |
|---|---|---|---|
| `saves.track_slot` | MinHook on `MakeValidSaveName` | slot of the current save or load (backups, side-car data) | always (capability `save.slot_tracking`) |
| early import hook (installed in `DllMain`) | game import `advapi32!RegQueryValueExA`, value "Personal" | separate User Files folder per install | `[saves] separate_user_files` |
| save path reset (call, not a patch) | `C_PcSave::SetSaveDirectory(<game>\sacore\userdata\GTA San Andreas User Files)` at core init | make saves follow the redirect | same |
| (test automation, a call) | `CMenuManager::DoSettingsBeforeStartingAGame(FrontEndMenuManager)` from the message loop, 3 s after the main menu appears; harness step `start_new_game` calls it while playing like the pause menu does | test automation; the menu's own routine keeps WinMain's first-start bookkeeping intact (R19) | `[testing] auto_start_new_game` |
| `game.exit_crash_guard` | MinHook on `RsEventHandler`: the `rsRWTERMINATE` (22) sent by WinMain right before `DestroyWindow` (recognised by its return address) returns `rsEVENTPROCESSED` without tearing RenderWare down | avoid the crash on exit (R20) | always (capability `stability.exit_crash_guard`) |
| `game.script_thread_guard` | MinHook on `CScriptsForBrains::StartNewStreamedScriptBrain` (1.0 0x46B270, newsteam RVA 0x706B0): returns without starting the brain while `CTheScripts::pIdleScripts` (1.0 0xA8B428, newsteam RVA 0x6C02CC) is null | avoid the crash when all 96 script threads are in use (casino interiors) | always (capability `stability.script_thread_guard`) |
| `testing.parallel_copies` (no patch, test runs only) | CdStreamInit names its request semaphore "CdStream" (1.0 0x406974, newsteam 0x406A4C). With a scenario armed: the early `CreateSemaphoreA` import hook makes the name private (newsteam), or SACore suspends and ends the idle `CdStreamThread` (1.0 0x406560) and starts a new one on a private semaphore (globals 1.0 sema 0x8E4004 / thread 0x8E4008 / id 0x8E4000 / queue head 0x8E3FF0 tail 0x8E3FF4; newsteam RVAs 0x55BBFC / 0x55BC00 / 0x55BBF8 / 0x55BBE8 / 0x55BBEC) | two copies of one build side by side in tests | only while `sacore\testing\scenario.toml` exists |
| `game.cdstream_sync` | MinHook on `CdStreamSync` (1.0 0x406460, a jump to the body at 0x156CD80; newsteam RVA 0x64E0): while `CdStream::gbThreaded` (1.0 0x8E3FE4, newsteam RVA 0x55BBDC) is set, the wait for a read checks the channel (`CdStream::gpChannels`, 1.0 0x8E3FFC, newsteam RVA 0x55BBF4; 0x30 bytes each) every 10 ms instead of waiting forever for its semaphore | no freeze when a read ends between the game's check and its "locked" flag | always (capability `stability.cdstream_sync`) |

## Content (build independent)

| Id | Target | Purpose | Setting |
|---|---|---|---|
| `content.file_overrides` | game import `kernel32!CreateFileA` (installed only when a running mod has a `content` folder) | open `<mod>\content\<game path>` instead of the game file for read-only `OPEN_EXISTING` opens; later mod in load order wins | add or remove content mods (`tools/modman.py`) |
