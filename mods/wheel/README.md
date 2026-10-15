# Weapon and radio wheel

Hold Caps Lock (setting `wheel_key`): on foot a wheel with the weapons CJ carries opens, in a vehicle a wheel with
the radio stations. The game runs in slow motion and the camera stands still while the wheel is open. Move the
mouse toward an entry and release the key to select it. Slow motion and size are settings in the F11 console.

- Build: `cmake --build --preset x86-release --target wheel`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod wheel --apply`
- Package: `python tools/package_mod.py wheel`
- Settings: `sacore\config\mods\wheel.toml` (declared in `mod.toml [config]`)
