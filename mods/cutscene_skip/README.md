# Cutscene skip

SACore mod `cutscene_skip`.

- Build: `cmake --build --preset x86-release --target cutscene_skip`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod cutscene_skip --apply`
- Package: `python tools/package_mod.py cutscene_skip`
- Settings: `sacore\config\mods\cutscene_skip.toml` (declared in `mod.toml [config]`)

## Settings

| Setting | Default | Effect |
|---|---|---|
| `skip_key` | 13 (Enter) | skips the current cutscene, skippable scripted scene or phone call |
| `auto_skip` | false | skips every cutscene and skippable scene automatically |
| `skip_calls` | key | phone calls: `key` = the skip key ends the call, `auto` = every call ends right after it is answered, `off` |
| `answer_calls` | false | answers the phone when it rings (not during missions) |

A skipped call counts like a call heard to the end: the mission or unlock it announces still follows. The mod
sends the call script into the game's own skip branch (the one the enter-vehicle button takes),
after checking the main.scm bytes at both offsets. With a modified main.scm the call options switch themselves off.
