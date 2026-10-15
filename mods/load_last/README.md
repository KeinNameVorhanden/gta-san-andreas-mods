# Load last save

When the game reaches the main menu for the first time, loads the newest valid save (menu slots, and quick and
autosaves unless `include_quick_and_auto = false`) exactly the way the menu's Load Game does. Hold Shift (setting
`hold_key`) while the game starts to stay in the menu. Does nothing when the test harness starts new games.

- Build: `cmake --build --preset x86-release --target load_last`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod load_last --apply`
- Package: `python tools/package_mod.py load_last`
- Settings: `sacore\config\mods\load_last.toml` (declared in `mod.toml [config]`)
