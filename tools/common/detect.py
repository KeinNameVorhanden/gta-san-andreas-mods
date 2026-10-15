"""Detect a game's engine, graphics API and installed mod loaders from its folder (game-agnostic, read-only).

Evidence is file layout, PE imports and marker strings; every finding names the evidence it came from, and
"unknown" is a valid answer.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

try:
    from . import pe
except ImportError:  # run as a script from tools/common
    import pe  # type: ignore[no-redef]

GRAPHICS_IMPORTS = {
    "d3d8.dll": "Direct3D 8", "d3d9.dll": "Direct3D 9", "d3d10.dll": "Direct3D 10", "d3d11.dll": "Direct3D 11",
    "d3d12.dll": "Direct3D 12", "dxgi.dll": "DXGI", "opengl32.dll": "OpenGL", "vulkan-1.dll": "Vulkan",
    "ddraw.dll": "DirectDraw",
}
# (engine, how to recognise it from the folder): each check returns evidence text or None.
ENGINE_MARKERS = [
    ("Unity", lambda root, exe: "UnityPlayer.dll" if (root / "UnityPlayer.dll").is_file() else
        (f"{exe.stem}_Data/" if exe and (root / f"{exe.stem}_Data").is_dir() else None)),
    ("Unreal Engine", lambda root, exe: next((p.relative_to(root).as_posix() for p in root.glob("*/Binaries/Win64")
                                             if p.is_dir()), None) or
        ("Engine/" if (root / "Engine" / "Binaries").is_dir() else None)),
    ("Source", lambda root, exe: next((p.relative_to(root).as_posix() for p in
                                      (root / "bin" / "engine.dll", root / "bin" / "win64" / "engine.dll")
                                      if p.is_file()), None)),
    ("Godot", lambda root, exe: next((p.name for p in root.glob("*.pck")), None)),
    ("GameMaker", lambda root, exe: "data.win" if (root / "data.win").is_file() else None),
    ("RenderWare", lambda root, exe: _marker_in(exe, (b"RenderWare", b"rwID_", b"RwEngine"))),
]
# Proxy-DLL loaders are recognised by a marker inside the DLL, never by the file name alone: games ship real DLLs
# with the same names (GTA SA's own vorbisFile.dll). (name, possible file names, markers inside the file)
PROXY_LOADERS = [
    ("Ultimate ASI Loader", ("dinput8.dll", "d3d9.dll", "d3d11.dll", "version.dll", "winmm.dll", "dsound.dll",
                             "vorbisFile.dll", "xinput1_3.dll", "winhttp.dll", "wininet.dll", "dxgi.dll"),
     (b"IsUltimateASILoader",)),
    ("ReShade", ("dxgi.dll", "d3d9.dll", "d3d11.dll", "d3d12.dll", "opengl32.dll", "ReShade64.dll", "ReShade32.dll"),
     (b"ReShade",)),
    ("ENBSeries", ("d3d9.dll", "d3d11.dll", "d3d8.dll"), (b"ENBSeries",)),
    ("BepInEx (Doorstop)", ("winhttp.dll", "version.dll"), (b"doorstop", b"Doorstop")),
    ("MelonLoader", ("version.dll", "winmm.dll"), (b"MelonLoader",)),
]
# Plugins and frameworks that are plain files or folders. (name, any of these paths)
PLUGIN_FILES = [
    ("CLEO", ("cleo.asi", "CLEO.asi", "scripts/cleo.asi")),
    ("Modloader", ("modloader.asi", "scripts/modloader.asi")),
    ("SilentPatch", ("SilentPatchSA.asi", "SilentPatchVC.asi", "SilentPatchIII.asi", "scripts/SilentPatchSA.asi")),
    ("BepInEx", ("BepInEx",)),
    ("MelonLoader", ("MelonLoader",)),
]


def _marker_in(exe: Path | None, markers: tuple[bytes, ...], limit: int = 64 << 20) -> str | None:
    if exe is None or exe.stat().st_size > limit:
        return None
    data = exe.read_bytes()
    found = next((m for m in markers if m in data), None)
    return f"'{found.decode()}' in {exe.name}" if found else None


@dataclass
class Finding:
    what: str
    evidence: str


@dataclass
class Report:
    folder: str
    exe: str | None = None
    pe: dict | None = None
    engine: list[Finding] = field(default_factory=list)
    graphics: list[Finding] = field(default_factory=list)
    loaders: list[Finding] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)


def main_executable(root: Path) -> Path | None:
    """The largest .exe in the folder that is not an obvious helper (installers, crash reporters, launchers)."""
    helpers = ("unins", "setup", "crash", "report", "redist", "vc_", "dxsetup", "launcher", "helper", "update")
    candidates = [p for p in root.glob("*.exe") if not any(h in p.name.lower() for h in helpers)]
    if not candidates:
        candidates = [p for p in root.glob("*/Binaries/Win64/*.exe")] + [p for p in root.glob("bin/*.exe")]
    return max(candidates, key=lambda p: p.stat().st_size, default=None)


def detect(root: Path) -> Report:
    report = Report(folder=str(root))
    if not root.is_dir():
        report.problems.append("folder does not exist")
        return report
    exe = main_executable(root)
    if exe is None:
        report.problems.append("no executable found")
    else:
        report.exe = exe.relative_to(root).as_posix()
        try:
            info = pe.read_pe(exe)
            report.pe = {"machine": info.machine, "timestamp": info.timestamp, "image_base": hex(info.image_base),
                         "aslr": info.aslr, "dotnet": info.is_dotnet, "pdb": info.pdb_path}
            for dll in info.imports:
                if dll in GRAPHICS_IMPORTS:
                    report.graphics.append(Finding(GRAPHICS_IMPORTS[dll], f"{exe.name} imports {dll}"))
            if not report.graphics and exe.stat().st_size < (64 << 20):
                # Packed or cracked executables hide their import table; the DLL names are still in the file.
                data = exe.read_bytes().lower()
                for dll, api in GRAPHICS_IMPORTS.items():
                    if dll.encode() in data:
                        report.graphics.append(Finding(api, f"'{dll}' string in {exe.name} (no import entry)"))
            if info.is_dotnet:
                report.engine.append(Finding(".NET", f"{exe.name} has a CLR header"))
        except (pe.NotPe, OSError, IndexError, ValueError) as error:
            report.problems.append(f"{exe.name}: {error}")
    for engine, check in ENGINE_MARKERS:
        evidence = check(root, exe)
        if evidence:
            report.engine.append(Finding(engine, evidence))
    for name, files, markers in PROXY_LOADERS:
        for file_name in files:
            path = root / file_name
            if path.is_file() and path.stat().st_size < (64 << 20):
                data = path.read_bytes()
                marker = next((m for m in markers if m in data), None)
                if marker:
                    report.loaders.append(Finding(name, f"{file_name} contains '{marker.decode()}'"))
    for name, paths in PLUGIN_FILES:
        present = next((rel for rel in paths if (root / rel).exists()), None)
        if present:
            report.loaders.append(Finding(name, present))
    if not report.engine:
        report.engine.append(Finding("unknown", "no marker matched"))
    asi_loaders = [f for f in report.loaders if f.what == "Ultimate ASI Loader"]
    if len(asi_loaders) > 1:
        report.problems.append("more than one ASI loader copy: " + "; ".join(f.evidence for f in asi_loaders)
                               + " (plugins may load twice)")
    return report
