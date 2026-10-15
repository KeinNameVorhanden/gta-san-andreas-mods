# Frame rate

Choose the frame limit (25 to 60 FPS) used while the game's own Frame Limiter option is on (`fps`). Above 30 FPS
the game's frame-dependent physics and timing are corrected on both builds (`physics_fixes`): swimming and diving
speed, doors, buoyancy, boats (speed and aquaplaning), Skimmer take-off, burnout, coasting, siren by horn tap,
helicopter rotors, pushing parked cars, climbing, vehicle turning, water cannon and AI car timing. Swimming at 60 FPS
covers the same distance as at 30 (measured by the `mod_frame_rate_physics` scenario). Train and tram wheels spin
at the right speed at any frame rate (`wheel_spin_fix`).

Some scripts run their own per-frame logic and break at higher frame rates; that logic is in the mission scripts,
not in the executable, so an exe patch cannot fix it. With `mini_games_at_30` the mod lowers the limit while they
run: pool, girlfriend dates, the gyms, Fender Ketchup (30 FPS) and the crack den in Just Business (50 FPS).

The fixes follow FramerateVigilante (MIT), reimplemented for both builds. Every patch site is checked against its
original bytes when the game starts; a site that does not match is left alone, and the log line "high frame rate
fixes: N sites patched, M not available on this build" counts them.

- Build: `cmake --build --preset x86-release --target frame_rate`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod frame_rate --apply`
- Package: `python tools/package_mod.py frame_rate`
- Settings: `sacore\config\mods\frame_rate.toml` (declared in `mod.toml [config]`)
