# Weapon pickup by key

Walking over a weapon no longer picks it up. Press the game's own pickup control while standing on it (the same
control the game asks for when a weapon would replace another one), or hold the extra key set in `pickup_key`.
Ammo for weapons CJ already carries, health, armour and money are still collected automatically.

- Build: `cmake --build --preset x86-release --target weapon_pickup`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod weapon_pickup --apply`
- Package: `python tools/package_mod.py weapon_pickup`
- Settings: `sacore\config\mods\weapon_pickup.toml` (declared in `mod.toml [config]`)
