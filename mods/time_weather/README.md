# Time and weather

Time and weather from the F11 console: freeze the clock, run it slower or faster, jump to an hour
(`set_hour`), move one hour forward with numpad +, and lock the weather (extra sunny, sunny, smog, cloudy, rain,
thunderstorm, fog, sandstorm, or the normal cycle). While a mission runs the mod steps back by default, because
missions set their own time and weather.

- Build: `cmake --build --preset x86-release --target time_weather`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod time_weather --apply`
- Package: `python tools/package_mod.py time_weather`
- Settings: `sacore\config\mods\time_weather.toml` (declared in `mod.toml [config]`)
