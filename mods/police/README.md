# Police switches

Police switches in the F11 console: never wanted (the game's own cheat flag), a wanted cap (0 to 6 stars),
faster wanted decay (one star every N seconds while no police is within 18 m), police helicopters that do not shoot,
or no police helicopters at all. By default missions keep the normal police, because some need a wanted level.

- Build: `cmake --build --preset x86-release --target police`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod police --apply`
- Package: `python tools/package_mod.py police`
- Settings: `sacore\config\mods\police.toml` (declared in `mod.toml [config]`)
