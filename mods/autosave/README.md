# Autosave

Saves automatically after every passed mission (and, if set, every N minutes of free roam) into its own rotating
files `GTASAas1.b` .. `GTASAas9.b`; the 8 menu slots are never touched. The save waits until the game is back in
free roam (mission cleanup done, no cutscene, CJ on foot unless `allow_in_vehicle`), and it never skips game time or
refills health like a safe house does. Load an autosave from the F11 console (Saves page).

- Build: `cmake --build --preset x86-release --target autosave`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod autosave --apply`
- Package: `python tools/package_mod.py autosave`
- Settings: `sacore\config\mods\autosave.toml` (declared in `mod.toml [config]`)
