# Radar zoom and GPS

- **Radar zoom** from 0.5 to 2 (above 1 shows more of the map); an optional key cycles 1, 1.5, 2 and 0.75.
- **GPS line**: set a waypoint on the pause-menu map and a purple line on the radar shows the road route to it.
  The route follows the game's car path network (read from `data\Paths\NODES*.DAT`) and is updated once a second
  while you drive. Not shown in interiors, cutscenes, the menu, or when the radar is switched off in the display
  options.

- Build: `cmake --build --preset x86-release --target radar`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod radar --apply`
- Package: `python tools/package_mod.py radar`
- Settings: `sacore\config\mods\radar.toml` (declared in `mod.toml [config]`)
