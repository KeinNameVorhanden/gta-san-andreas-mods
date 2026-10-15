# Vehicle tweaks

- Cars lying on their roof no longer catch fire and explode (`no_upside_down_damage`, on by default).
- With mouse steering switched on in the game's controls, the wheels return to centre while the mouse rests
  (`mouse_steering_recenter`, 0 = game behaviour). Keyboard and pad steering are unchanged.

- Build: `cmake --build --preset x86-release --target vehicle_tweaks`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod vehicle_tweaks --apply`
- Package: `python tools/package_mod.py vehicle_tweaks`
- Settings: `sacore\config\mods\vehicle_tweaks.toml` (declared in `mod.toml [config]`)
