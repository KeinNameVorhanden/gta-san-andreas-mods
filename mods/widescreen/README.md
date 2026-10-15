# Widescreen FOV

On wide screens the game keeps the 4:3 width of the picture and cuts the top and bottom. This mod widens the
field of view to match the screen (Hor+), for every camera including aiming, and can add a few degrees on top
(`extra_fov`). Works on both builds; it needs the game's Widescreen display option on for 16:9.

Not included: HUD and radar proportions. They can only be judged in the running game (SilentPatch may already
correct them on the Steam build); left for a visual check.

- Build: `cmake --build --preset x86-release --target widescreen`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod widescreen --apply`
- Package: `python tools/package_mod.py widescreen`
- Settings: `sacore\config\mods\widescreen.toml` (declared in `mod.toml [config]`)
