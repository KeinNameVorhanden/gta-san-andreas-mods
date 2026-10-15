# SACore mods

All mods live in `mods/<id>/` (source, `mod.toml` with the settings, README). Every setting can be changed live in
the F11 console (Settings page). Keys are Windows key codes and can be changed there as well.

| Mod | What it does | Default keys |
|---|---|---|
| `devtools` | In-game console (status, mods, settings, saves, save import, log, crashes) | F11 |
| `quicksave` | Quick save in its own rotating files, free roam only; quick load | F5, F9 twice |
| `autosave` | Saves after every passed mission (and optionally on a timer) in its own rotating files | - |
| `load_last` | Loads the newest save when the game reaches the main menu | hold Shift: stay in the menu |
| `cutscene_skip` | Skips cutscenes and skippable scripted scenes, or all of them automatically; ends phone calls (they still count for the story) and can answer them | Enter |
| `mission_pass` | Passes the current mission; story runner plays the whole story up to a chosen mission (`run_to`, 101 targets plus "Story complete"), answering phone calls, buying the properties the story needs, winning the races | F6 twice |
| `mission_retry` | After a failed mission: back to the mission marker | F4 |
| `photo_mode` | HUD on/off; photo mode with slowed world and free camera | F8, F7 |
| `stat_boost` | Faster stamina, muscle, lung, driving and weapon skill gains | - |
| `time_weather` | Freeze or speed up the clock, set the hour, lock the weather | numpad + (one hour) |
| `police` | Never wanted, wanted cap, faster decay, no heli shots, no police helis | - |
| `vehicle_tweaks` | No damage on the roof; mouse steering returns to centre | - |
| `weapon_pickup` | Weapons on the ground only by the pickup control | - |
| `mouse_aim` | Separate aim sensitivity, per-axis invert, raw mouse input | - |
| `world_density` | Ped and traffic density, draw distance beyond the slider | - |
| `radar` | Radar zoom; GPS route line to the map waypoint | (zoom key off) |
| `wheel` | Weapon wheel on foot, radio wheel in vehicles, with slow motion | hold Caps Lock |
| `teleport` | Teleport to the map waypoint; fast travel between owned safehouses | F2, F3 |
| `trip_skip` | The game's own trip skip on long mission drives | (game prompt) |
| `frame_rate` | Frame limit up to 60 FPS with physics and timing fixes above 30 (swimming, diving, doors, buoyancy, boats, Skimmer, burnout, coasting, siren, heli rotors, pushing cars, climbing, vehicle turning, water cannon, AI cars); lower limit where scripts break (pool, dates, gyms, Fender Ketchup, crack den) | - |
| `widescreen` | Hor+ field of view on wide screens | - |
| `controller` | XInput controllers on the 1.0 US build | - |
| `streaming_memory` | 256 MB streaming budget instead of 50 MB (less pop-in); with the 4 GB flag (`tools/laa.py`) every world model and texture is loaded once in the background and kept (`preload`) | - |
| `session_stats` | Example mod: play statistics | - |

Community saves: put downloaded save files into `<game>\sacore\import`; the console's Saves page lists them,
checks them and copies a valid one into a menu slot (the replaced save is backed up). SACore downloads nothing
itself; you put the files there.

## Verification

Runtime scenarios in `tests/runtime/scenarios/` (run with `tools/run_runtime_tests.py` on both builds):

| Scenario | Covers |
|---|---|
| `mod_quicksave`, `quick_load`, `mod_quicksave_freeroam` | quick save refusal during missions, save and load path, F5/F9 in free roam |
| `mod_cutscene_skip`, `mod_cutscene_skip_key` | automatic skip and Enter |
| `mod_phone_call_skip` | story chain: Tenpenny's call after House Party ends right after it is answered and still unlocks Burning Desire |
| `mod_mission_pass`, `mod_mission_pass_redirect` | debug skip and pass-code jump |
| `mod_story_runner` | story runner from a new game, short run, saved to slot 7 and loaded |
| `mod_story_saves_fresh`, `mod_story_saves_past` | story saves: a new game loads the story save of a later mission, a game at the end of the story loads Badlands' |
| `mod_story_runner_ls`, `_badlands`, `_sf`, `_desert`, `_lv`, `_finale`, `_end` | the whole story in seven chunks, each from the save the previous one made (slots 6 to 1): Los Santos from a new game, the Badlands, San Fierro, the desert, Las Venturas, Los Santos up to End of the Line, and End of the Line to the end |
| `mod_qol_freeroam` | streaming memory, GPS graph and route, mission retry after death, autosave after a pass, HUD and photo mode, time and weather, police heli patch, frame limit, raw mouse, waypoint teleport, safehouse fast travel |
| `mod_load_last_import` | save import into a slot, load of the newest save from the main menu |
| `mod_frame_rate_physics` | swim distance in 6 s at 30 and at 60 FPS must agree within 10 % (0.50 without the fixes, 1.00 with them) |
| `mod_frame_rate_60` | 60 FPS with every fix active in traffic and in the sea, frames keep advancing |
| `mod_streaming_preload` | with the 4 GB flag: the world preload reaches 100 % without hitting the address-space guard, then Los Santos, San Fierro and Las Venturas keep running |

Checked only for "hook or patch installed, game keeps running" (behaviour needs a player): `stat_boost`,
`vehicle_tweaks`, `weapon_pickup`, `world_density`, `wheel`, `trip_skip`, `widescreen`, `controller`. They are
on the manual checklist in `docs/COMPATIBILITY.md`.
