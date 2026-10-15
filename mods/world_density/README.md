# Density and draw distance

Pedestrian and traffic density (0 to 3 times the game's) and a draw distance beyond the menu slider (up to 3.0;
the slider stops at 1.8). Cutscenes, and by default missions, keep the density they set. For large draw distances
also raise the streaming memory (streaming_memory mod), otherwise distant objects pop in late.

- Build: `cmake --build --preset x86-release --target world_density`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod world_density --apply`
- Package: `python tools/package_mod.py world_density`
- Settings: `sacore\config\mods\world_density.toml` (declared in `mod.toml [config]`)
