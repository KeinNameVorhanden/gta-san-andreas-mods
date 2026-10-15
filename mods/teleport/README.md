# Waypoint teleport and fast travel

- **F2** teleports CJ (with the vehicle he sits in) to the waypoint you set on the pause-menu map, onto the
  ground, and removes the waypoint.
- **F3** travels to the next safehouse CJ owns (every safehouse with a save icon on the map); press again for the
  next one.

Outdoors only, and off during missions unless `allow_during_missions` is set (teleporting can break mission
scripts that wait for CJ to drive somewhere).

- Build: `cmake --build --preset x86-release --target teleport`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod teleport --apply`
- Package: `python tools/package_mod.py teleport`
- Settings: `sacore\config\mods\teleport.toml` (declared in `mod.toml [config]`)
