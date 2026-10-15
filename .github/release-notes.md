SACore and all mods for the classic PC **GTA San Andreas** (Steam `gta-sa.exe` newsteam r2, or 1.0 US `gta_sa.exe`).
Unofficial fan project; you need your own copy of the game.

**Install** (Windows, Python 3.11 or newer):

1. Copy the game out of the Steam folder (SACore refuses to install into `steamapps`), for example to
   `D:\Games\GTASA-Play\steam`.
2. Unpack `SACore-<version>.zip`, open a terminal in the unpacked folder and run a dry run, then the install:

   ```bat
   python install.py --game "D:\Games\GTASA-Play\steam" install --with all
   python install.py --game "D:\Games\GTASA-Play\steam" --apply install --with all
   ```

   `--with all` installs every mod; name single ones instead (`--with devtools,quicksave,cutscene_skip`).
3. Start the game from the copy. **F11** opens the console with every mod's settings.

The package contains SACore, all 24 mods, [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
9.7.4 and [SilentPatch](https://github.com/CookiePLMonster/SilentPatch) 34.1 (both MIT, license texts included).
Update with `--apply update`, undo with `--apply recover rollback` or `--apply uninstall`.

What changed: [docs/RELEASE_NOTES.md](https://github.com/KeinNameVorhanden/gta-san-andreas-mods/blob/main/docs/RELEASE_NOTES.md).
Full guide: [README](https://github.com/KeinNameVorhanden/gta-san-andreas-mods#readme).
