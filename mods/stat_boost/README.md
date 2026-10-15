# Stat boost

Multiplies how fast stats grow: body (stamina, muscle, lung capacity), vehicles (driving, flying, motorbike,
cycling) and weapon skills, each x3 by default; fat gain and fat loss stay x1 unless changed. 0 stops a group from
growing. Only the gain per step changes, the game's own limits (1000 for skills) stay. Settings in the F11 console.

- Build: `cmake --build --preset x86-release --target stat_boost`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod stat_boost --apply`
- Package: `python tools/package_mod.py stat_boost`
- Settings: `sacore\config\mods\stat_boost.toml` (declared in `mod.toml [config]`)
