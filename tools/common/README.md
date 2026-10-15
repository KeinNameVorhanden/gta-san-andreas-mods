# Modding workshop utilities (game-agnostic)

Collected from the tools that proved useful in this project. Nothing in here knows about GTA San Andreas;
SA-specific knowledge (known builds, addresses, SACore layout) stays in `tools/*.py`. Standard library only,
Python 3.11+.

| Module | Use |
|---|---|
| `workshop.py` | command line: `detect`, `manifest create/verify` |
| `detect.py` | engine (Unity, Unreal, Source, Godot, GameMaker, RenderWare, .NET), graphics API, proxy-DLL loaders (identified by a marker inside the DLL, never by file name alone), plugin frameworks; every finding names its evidence |
| `pe.py` | PE header facts: machine, timestamp, image base, ASLR, sections, imported DLLs, .NET, PDB path |
| `files.py` | SHA-256 of files, tree manifests and their diff, Windows version resources |
| `packaging.py` | archive member paths checked before anything is written (absolute, drive, UNC, `..`), extraction, deterministic zips |
```
python tools/common/workshop.py detect "<game folder>"
python tools/common/workshop.py manifest create "<original install>" pinned.json
python tools/common/workshop.py manifest verify "<original install>" pinned.json```

Used by: `tools/inventory.py`, `tools/hash_manifest.py`, `tools/compat_check.py`, `tools/modman.py`.

Validated (2026-10-13): both GTA SA test copies (RenderWare, Direct3D 9, ASI loader, SilentPatch), a Unity game
(BloonsTD6: UnityPlayer.dll, x64, IL2CPP), an Unreal game (Deep Rock Galactic), and two folders without an
executable (planted negatives: "unknown", "no executable found"). Tests: `tests/tools/test_common.py`.
