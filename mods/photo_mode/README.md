# HUD toggle and photo mode

- **F8** hides or shows the whole HUD (radar, money, crosshair, help text, big messages).
- **F7** photo mode (free roam only): HUD off, the world slowed almost to a stop, free camera. W A S D move,
  Q and E go down and up, the mouse looks around, Shift moves faster. F7 again returns to the normal camera.

Keys, speed and mouse sensitivity are settings in the F11 console.

- Build: `cmake --build --preset x86-release --target photo_mode`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod photo_mode --apply`
- Package: `python tools/package_mod.py photo_mode`
- Settings: `sacore\config\mods\photo_mode.toml` (declared in `mod.toml [config]`)
