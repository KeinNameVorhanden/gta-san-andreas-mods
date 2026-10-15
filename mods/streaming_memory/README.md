# Streaming memory

Raises the game's streaming budget from 50 MB to 256 MB (setting `memory_mb`, up to 2048), so buildings, cars and
textures load earlier and pop in less, especially with a higher draw distance. Optionally keeps more vehicle models
loaded (more varied traffic) and streams the area around CJ completely right after a save is loaded.

## Preload (needs the 4 GB address space)

With the Large Address Aware flag set in the game copy's exe (`python tools/laa.py --game <copy> --apply`, see
docs/INSTALL.md), `preload = "auto"` loads every world model with its textures once in the background and keeps
them loaded, so nothing pops in again anywhere on the map. Without the flag `auto` does nothing.

| Setting | Effect |
|---|---|
| `preload = "auto"` | world when the game has the 4 GB address space, else off (default) |
| `preload = "world"` | buildings, objects, interiors and weapons: 14312 models, about 290 MB of files |
| `preload = "everything"` | also vehicles and peds (pinned again every 30 s, the population code releases them) |
| `preload = "off"` | nothing is kept; models pinned earlier may be unloaded again |
| `preload_reserve_mb` | the preload stops when less address space than this is left (default 600 MB) |

Measured on both builds (mod_streaming_preload, 2026-10-15): the world preload takes about 2 minutes after a new
game or a load (25 % per half minute), uses 883 MB of streaming memory and leaves 2.2 GB of address space free.
While it runs the budget is at least 1536 MB. It never gets in the game's way: it only adds requests while the game
has none of its own pending, and it asks for files in archive order so each read fetches up to 16 of them. A new
game or a load unloads the world models (the game's own reset), so the preload starts again after each.

- Build: `cmake --build --preset x86-release --target streaming_memory`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod streaming_memory --apply`
- Package: `python tools/package_mod.py streaming_memory`
- Settings: `sacore\config\mods\streaming_memory.toml` (declared in `mod.toml [config]`)
