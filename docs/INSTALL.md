# Installing SACore

SACore is installed into a **play copy** of the game, never into the Steam folder. Steam can then update or verify
its own folder without touching your modded game, and your modded game cannot damage the Steam install.

Requirements: Windows 10/11, Python 3.11 or newer (`python --version`), about 5 GB free per play copy.

Every command below is a dry-run that only prints what it would do. Add `--apply` (right after `--game ...`) to
carry it out. Nothing is ever deleted: replaced or removed files go to `<play copy>\sacore\install\backups\`.

## 1. Make a play copy

Close Steam's game first. Copy the whole Steam folder (here to `E:\Games\GTASA-Play\steam`):

```
robocopy "E:\SteamLibrary\steamapps\common\Grand Theft Auto San Andreas" "E:\Games\GTASA-Play\steam" /E
```

Keep a pinned copy of the executable so a Steam update can never leave you without the supported build:

```
copy "E:\Games\GTASA-Play\steam\gta-sa.exe" "E:\Games\GTASA-Play\pinned-gta-sa.exe"
```

The play copy runs without Steam.

## 2. Optional: a 1.0 US copy

The 1.0 US build has the most mods available. Make a second play copy as in step 1 (for example
`E:\Games\GTASA-Play\us10`) and downgrade it with the project tools (needs the repository with `downloads\`):

```
python tools/apply_downgrade.py --game "E:\Games\GTASA-Play\us10" --patches downloads/patches/x --xdelta downloads/xdelta/x/xdelta3-3.2.0-windows-x86_64/xdelta3.exe
```

Run it again with `--apply`. Every input and output file is hash-checked; the result is `gta_sa.exe`
(1.0 US Hoodlum, SHA-256 `a559aa77...0645cbd26`). Saves of the two builds are not compatible; the installer gives
the 1.0 US copy its own save folder automatically.

## 3. Install

Unpack `SACore-<version>.zip` anywhere, open a terminal in the unpacked `SACore-<version>` folder and run:

```
python install.py --game "E:\Games\GTASA-Play\steam" install --with devtools,session_stats
python install.py --game "E:\Games\GTASA-Play\steam" --apply install --with devtools,session_stats
```

Installed: Ultimate ASI Loader 9.7.4 (`dinput8.dll`), SilentPatch 34.1 and SACore (`scripts\`), and the optional
mods you picked (`devtools`: in-game console on F11; `session_stats`: play statistics, also the example mod).
A file that was already there (for example another ASI loader) is backed up and comes back on uninstall.

The installer refuses: the Steam folder, an executable that is not one of the two supported builds, and a
running game. It warns about tools known to break this setup (Modloader, III.VC.SA.WindowedMode).

## 4. Verify

```
python install.py --game "E:\Games\GTASA-Play\steam" verify
```

Then start the game (double-click the exe). Expected: a window with a frame, no intro movies, the main menu.
The window has the game's resolution (640x480 on a fresh install): choose a larger one in Options, Display Setup.
`<play copy>\sacore\logs\sacore.log` begins with the detected build and ends each session with
`game window closing after ... frames`. `sacore\diagnostics\capabilities.txt` lists what works on this build.

## 5. Configure

`<play copy>\sacore\config\core.toml` is created with every setting and its description on the first start.
Most used:

| Setting | Default | Meaning |
|---|---|---|
| `[window] windowed` | `true` | window instead of exclusive fullscreen |
| `[window] client_width` | `0` | `0`: the window is exactly the resolution chosen in the game (Options, Display Setup); a number: that width, scaled |
| `[window] run_in_background` | `true` | keep running without focus (the mouse is never trapped while in the background) |
| `[window] focus_on_start` | `true` | `false`: start behind other windows |
| `[audio] start_muted` | `false` | start muted |
| `[game] skip_intro_movies` | `true` | skip logos and intro |
| `[saves] separate_user_files` | `false` (`true` on the 1.0 US copy) | saves and settings in `sacore\userdata` instead of Documents |
| `[saves] backups_per_slot` | `10` | automatic backups kept per save slot |
| `[stability] crash_dumps` | `true` | crash report and minidump in `sacore\crashes` |
| `[mods] enabled` | `true` | `false`: no mods are loaded |
| `[patches] disabled` | `""` | switch single SACore patches off (ids in `sacore\diagnostics\patches.txt`) |

A broken file is never fatal: SACore uses the defaults, keeps your file as `core.toml.broken` and logs why.

## 6. Mods

Use the mod manager from the repository (`python tools/modman.py --game <play copy> list`): enable, disable,
install from zip, profiles. See `docs/DEVELOPER_GUIDE.md` section 10.

### 4 GB address space (for the streaming_memory preload)

The game is a 32-bit program and gets 2 GB of address space. With the Large Address Aware flag set in its exe it
gets 4 GB on 64-bit Windows, enough to keep every world model and texture loaded (`streaming_memory`, setting
`preload`). Set it in the play copy only, never in the Steam folder (the tool refuses that), with the game closed:

```
python tools/laa.py --game "E:\Games\GTASA-Play\steam"
python tools/laa.py --game "E:\Games\GTASA-Play\steam" --apply
```

The exe before the change is kept in `<play copy>\sacore\install\backups\`; `--remove --apply` clears the flag
again. SACore, the installer and the tools still recognise the build. `sacore.log` says
"4 GB address space (Large Address Aware flag set)" when it worked.

## 7. Update

Unpack the newer package and run `update` from its folder (dry-run, then `--apply`):

```
python install.py --game "E:\Games\GTASA-Play\steam" --apply update
```

Your optional component choice, settings and saves are kept. The previous files go to a backup.

## 8. Recover

| Problem | Command |
|---|---|
| The game crashed | send `<play copy>\sacore\crashes\<time>.zip` (one file per crash, written silently) |
| The game crashes or misbehaves after adding a mod | `python install.py --game <play copy> --apply recover safe-mode` (all mods off), later `recover normal` |
| A new SACore version misbehaves | `python install.py --game <play copy> --apply recover rollback` (back to the previous version) |
| A save is damaged | `python tools/saves.py --game <play copy> backups <slot>`, then `--apply restore <slot> <backup>` |
| Steam replaced the executable | copy `pinned-gta-sa.exe` back as `gta-sa.exe` (only matters if you installed into a Steam-managed folder) |

More symptoms and fixes: `docs/COMPATIBILITY.md`, Troubleshooting.

## 9. Uninstall

```
python install.py --game "E:\Games\GTASA-Play\steam" --apply uninstall
```

Removes exactly the files the installer put there (files you changed by hand are kept and named) and puts back
what was there before. Kept: `sacore\config`, logs, crash reports, saves in `sacore\userdata` and save backups.
Without SACore the game uses `Documents\GTA San Andreas User Files` again.
