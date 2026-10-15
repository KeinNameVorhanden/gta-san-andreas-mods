# Mouse and aim

Mouse settings the game does not have: a separate sensitivity while aiming (right mouse button by default) and
while looking around, inverting each axis on its own, and raw mouse input from Windows (no acceleration). All
multiply on top of the game's own mouse slider; the defaults (1.0, no invert, no raw input) change nothing.

- Build: `cmake --build --preset x86-release --target mouse_aim`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod mouse_aim --apply`
- Package: `python tools/package_mod.py mouse_aim`
- Settings: `sacore\config\mods\mouse_aim.toml` (declared in `mod.toml [config]`)
