# Runtime Regression Scenarios

Every fixed bug or crash gets a scenario here. "Automated" scenarios run through the runtime harness
(`tools/run_runtime_tests.py`); "manual" ones need a player. Known crashes of the stock game come from the SilentPatch
changelog and its author's blog, plus our own runtime observations.

| Id | Problem | Builds | Fix | How it is checked | Type |
|---|---|---|---|---|---|
| R1 | Intro movies never finish (DirectShow), game stuck in state 2 | us10 observed | `game.skip_intro` | game reaches state 7 within 10 s of start | automated |
| R2 | Device Selection dialog blocks startup on multi-adapter systems | both | `stability.skip_device_selection` (CBT hook) | log line "Device Selection dialog confirmed automatically" and first frame within 10 s without launcher help | automated |
| R3 | Game pauses when the window loses focus | both | engine run in background | frames keep increasing after a simulated focus loss | automated |
| R4 | Exclusive fullscreen switches the display mode and rearranges the desktop | both | `window.windowed = true` default | CreateDevice log shows forced windowed; window has a caption | automated |
| R5 | Unknown executable must not be modified | any | build hash check | log says "SACore stays inactive"; no patch lines | automated (launcher `--no-dialog-confirm`) |
| R6 | A crashing mod must not take the game down | both | SEH-guarded mod entry | test_crash disabled, test_ok running | automated |
| R7 | A crash must leave a report and a dump | both | crash handler | deliberate crash produces .txt naming the faulting module and a .dmp | automated (`testing.crash_after_seconds`) |
| R8 | DirectPlay install prompt on start | all (vanilla) | SilentPatch | prompt not seen with the stack installed | manual (Windows dialog) |
| R9 | Skimmer freeze on Windows 11 24H2 | all | SilentPatch | play: approach a Skimmer spawn | manual |
| R10 | Streaming deadlock at interiors (black screen) | all | SilentPatch | play: enter and leave interiors several times | manual |
| R11 | Mouse locks after leaving the menu | all | SilentPatch | play: open and close the menu, move the camera | manual |
| R12 | Save/load corruption across builds sharing User Files | both | `[saves] separate_user_files` (0.8.0) | saves scenario: slot file valid in `sacore\userdata`, nothing written to Documents | automated |
| R13 | Normal exit ended with 0xC0000409 on newsteam after the crash filter hook (SACore 0.3.0) | newsteam | filter re-assertion instead of a hook (0.3.1) | smoke scenario requires exit code 0 | automated |
| R14 | Calling the save-slot routine as `__cdecl` corrupted the caller's stack | both | `__stdcall` declaration (0.8.0) | saves scenario exits with code 0 | automated |
| R15 | Test new-game trigger loaded an existing save (load flag) and crashed | both | trigger writes `gGameState = 8` (0.8.0) | saves scenario run twice: second run starts with a save present | automated |
| R16 | Game running in the background trapped and moved the user's mouse (ClipCursor/SetCursorPos act system-wide) | both | mouse guards: those calls only take effect while the game window is in front; `[window] focus_on_start = false` for test copies; launchers start without activation (0.8.1) | shutdown log reports blocked background mouse grabs | automated |
| R17 | Window frame flickered and the window jumped in size at every start: the game switches its window to WS_POPUP after device creation and SACore repaired it one frame later | both | the game's SetWindowLongA/SetWindowPos on its own window are absorbed; window client = game resolution (0.9.3) | smoke: both window guard patches installed and the log names the window size; measured once with a 5 ms window poll: one shown state, no style change | automated |
| R18 | A game killed without shutdown (no WM_DESTROY) must not break the next start or the last save | both | verification only | incomplete_shutdown: no crash report, restart reaches gameplay, the save made before the kill is still valid | automated |
| R19 | "Start new game" from the pause menu crashed (collision pool overflow in `CColStore::AddColSlot("procobj")`): the test auto start had skipped WinMain's first-start bookkeeping | both | auto start through `DoSettingsBeforeStartingAGame` | new_game_restart: restart while playing, money back to 0, save after the intro; the old trigger crashes in this scenario (negative control, 2026-10-13) | automated |
| R20 | Crash on exit (`RwFreeListFree` with a destroyed free list, then plugin destructors) after a new game | both | `game.exit_crash_guard` skips WinMain's final RenderWare teardown | new_game_restart and example_mod exit with code 0; 3/3 on a fresh 1.0 US copy that crashed 3/3 before | automated |
| R21 | With separate User Files, 1.0 US still wrote its settings (gta_sa.set) to the shared Documents folder: its User Files lookup does not pass through the import table SACore redirects | us10 (newsteam: hook installed as well) | `saves.user_files_folder` corrects the game's User Files buffer on every call (0.9.6) | probe on a fresh 1.0 US copy: settings file in sacore/userdata, none in Documents (before: Documents) | automated (probe script; scenario check below) |
| R22 | F10 (or a bare Alt) froze the windowed game until F10 was pressed again: Windows' keyboard menu loop for the frame's system menu | both | SC_KEYMENU without a character is ignored in windowed mode; Alt+Space still opens the window menu (0.9.10) | input_guards: the posted menu request is ignored and logged (without the fix the log line is missing; the freeze itself needs focus, user check) | automated + manual |
| R24 | Once (1 of 9 runs, 1.0 US) "In the Beginning" kept running after mission_pass jumped to its end; not reproduced in 8 further runs | us10 | mission_pass logs thread position, stack depth and wake time 5 s after a jump that has not finished (1.1.5) | mod_mission_pass, mod_mission_pass_redirect | watch (diagnostic in place) |
| R23 | With run-in-background the player reacted to Shift typed in other windows (GetKeyState/GetAsyncKeyState read the physical keyboard) | both | keyboard reads answer 'not pressed' while the game window is not in front (0.9.10) | input_guards: guard installed, background reads answered (1256 in one run) | automated |
