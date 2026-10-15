# {{NAME}}

SACore mod `{{ID}}`.

- Build: `cmake --build --preset x86-release --target {{ID}}`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod {{ID}} --apply`
- Package: `python tools/package_mod.py {{ID}}`
- Settings: `sacore\config\mods\{{ID}}.toml` (declared in `mod.toml [config]`)
