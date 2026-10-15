# Trip skip

During missions with one yellow destination on the radar, more than 400 m away, while CJ drives a road
vehicle, the game's own trip-skip prompt appears (the one some missions show after a failed attempt). Accept it
and the game fades out, moves the car to just before the destination and fades in. The skip stops 40 m short so
the mission's own arrival still happens. Not offered during chases (wanted level) by default.

- Build: `cmake --build --preset x86-release --target trip_skip`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod trip_skip --apply`
- Package: `python tools/package_mod.py trip_skip`
- Settings: `sacore\config\mods\trip_skip.toml` (declared in `mod.toml [config]`)
