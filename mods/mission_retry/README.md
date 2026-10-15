# Mission retry

After a failed mission (failed, wasted or busted) a help box offers a retry: **F4** puts CJ back onto the
marker that started the mission, and the game's own trigger starts it again. Weapons lost by dying are not given
back. Offered only for missions that start from a marker outdoors (not for phone calls or missions that chain
straight into the next one), for two minutes after the fail.

- Build: `cmake --build --preset x86-release --target mission_retry`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod mission_retry --apply`
- Package: `python tools/package_mod.py mission_retry`
- Settings: `sacore\config\mods\mission_retry.toml` (declared in `mod.toml [config]`)
