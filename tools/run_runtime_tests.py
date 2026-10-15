"""Run in-game test scenarios through the SACore runtime harness.

For every (install, scenario) pair: copies the scenario to <game>/sacore/testing/scenario.toml, starts the game,
waits for it to exit (the harness closes it), reads results.json, removes the scenario file again, and prints a
summary. Scenarios with `expect_crash = true` pass when the game crashed and SACore wrote a crash report.

Requires a test copy with SACore deployed (tools/deploy_dev.py) and, for gameplay steps,
`[testing] auto_start_new_game = true` in its sacore/config/core.toml.

Usage:
    python tools/run_runtime_tests.py --game <us10 test copy> --game <steam30 test copy> \
        --scenario tests/runtime/scenarios [--json-out runtime-results]
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

EXE_NAMES = ("gta_sa.exe", "gta-sa.exe")
CRASH_EXIT_CODE = 0xC0000005


def no_activate_startupinfo() -> subprocess.STARTUPINFO:
    """Start the game without activating its window, so a test run never takes the user's focus or mouse."""
    info = subprocess.STARTUPINFO()
    info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    info.wShowWindow = 4  # SW_SHOWNOACTIVATE
    return info


class MemorySampler:
    """Samples the private memory of a process (soak tests: leaks and growth)."""

    def __init__(self, pid: int):
        import ctypes
        import ctypes.wintypes as wt

        class Counters(ctypes.Structure):
            _fields_ = [("cb", wt.DWORD), ("PageFaultCount", wt.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
                        ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                        ("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]
        self._ctypes = ctypes
        self._counters = Counters
        self._handle = ctypes.windll.kernel32.OpenProcess(0x0400 | 0x0010, False, pid)
        self.samples: list[tuple[float, int]] = []

    def sample(self, seconds: float) -> None:
        counters = self._counters()
        counters.cb = self._ctypes.sizeof(counters)
        if self._handle and self._ctypes.windll.psapi.GetProcessMemoryInfo(self._handle, self._ctypes.byref(counters),
                                                                            counters.cb):
            self.samples.append((seconds, counters.PrivateUsage))

    def summary(self) -> str:
        if len(self.samples) < 3:
            return "memory: too few samples"
        # Ignore the first minute (loading); compare the rest.
        settled = [s for s in self.samples if s[0] >= 60] or self.samples
        first, last = settled[0], settled[-1]
        peak = max(v for _, v in self.samples)
        minutes = max((last[0] - first[0]) / 60.0, 1 / 60)
        growth = (last[1] - first[1]) / (1024 * 1024) / minutes
        return (f"memory: private {first[1] / 2**20:.0f} -> {last[1] / 2**20:.0f} MB after loading, peak "
                f"{peak / 2**20:.0f} MB, growth {growth:+.2f} MB/min over {minutes:.1f} min")


def hold_instance_name() -> object | None:
    """Lets several game copies run at once. The game refuses a second instance when its named event (or mutex)
    "Grand theft auto San Andreas" already exists. The runner creates that name first, with an empty access list: the
    games' own create call then fails with "access denied" instead of "already exists", and every copy starts. Held
    for the whole run; nothing in the game folders changes. Returns the handle (keep it alive) or None."""
    import ctypes
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    advapi32 = ctypes.WinDLL("advapi32", use_last_error=True)

    class SecurityAttributes(ctypes.Structure):
        _fields_ = [("nLength", wintypes.DWORD), ("lpSecurityDescriptor", ctypes.c_void_p),
                    ("bInheritHandle", wintypes.BOOL)]

    descriptor = ctypes.create_string_buffer(64)  # SECURITY_DESCRIPTOR_MIN_LENGTH is 20
    acl = ctypes.create_string_buffer(64)
    if not advapi32.InitializeSecurityDescriptor(descriptor, 1) or not advapi32.InitializeAcl(acl, 64, 2):
        return None
    if not advapi32.SetSecurityDescriptorDacl(descriptor, True, acl, False):  # present but empty: no access at all
        return None
    attributes = SecurityAttributes(ctypes.sizeof(SecurityAttributes), ctypes.cast(descriptor, ctypes.c_void_p), False)
    kernel32.CreateEventW.restype = wintypes.HANDLE
    handle = kernel32.CreateEventW(ctypes.byref(attributes), False, True, "Grand theft auto San Andreas")
    if not handle:
        return None
    hold_instance_name.keep = (descriptor, acl, attributes)  # the descriptor must outlive the call only; kept anyway
    return handle


def keep_awake() -> None:
    """The PC must not go to sleep in the middle of a run, and displays that are on stay on (a sleeping one is not
    woken). Test games run with the monitors in power save since the core drops the main-menu request a skipped main
    menu left pending, but Windows then presents only about 30 frames a second, so mod_frame_rate_60 and
    mod_frame_rate_physics cannot reach 60 FPS (measured 2026-10-15: 46/50 with the monitors asleep, the other two
    scenarios on both builds). Until this process exits."""
    import ctypes
    es_continuous, es_system_required, es_display_required = 0x80000000, 0x00000001, 0x00000002
    ctypes.windll.kernel32.SetThreadExecutionState(es_continuous | es_system_required | es_display_required)


def sha256_of(path: Path) -> str:
    """Build identity of a game executable (the 4 GB flag does not make another build)."""
    import laa
    return laa.identity(path.read_bytes())[0]


def find_exe(game: Path) -> Path | None:
    for name in EXE_NAMES:
        if (game / name).is_file():
            return game / name
    return None


def load_toml_flag(path: Path, key: str) -> bool:
    import tomllib
    return bool(tomllib.loads(path.read_text(encoding="utf-8")).get(key, False))


def render_toml(data: dict) -> str:
    """Minimal TOML writer for the flat two-level tables used in core.toml."""
    def value(v):
        if isinstance(v, bool):
            return "true" if v else "false"
        if isinstance(v, (int, float)):
            return repr(v)
        return '"' + str(v).replace("\\", "\\\\").replace('"', '\\"') + '"'
    lines = [f"{k} = {value(v)}" for k, v in data.items() if not isinstance(v, dict)]
    for section, table in data.items():
        if isinstance(table, dict):
            lines.append(f"\n[{section}]")
            lines += [f"{k} = {value(v)}" for k, v in table.items()]
    return "\n".join(lines) + "\n"


def apply_overrides(config: Path, overrides: dict) -> str | None:
    """Merges scenario [config] overrides into core.toml; returns the original text for restoring."""
    import tomllib
    if not overrides:
        return None
    original = config.read_text(encoding="utf-8") if config.exists() else ""
    merged = tomllib.loads(original) if original else {}
    for section, table in overrides.items():
        merged.setdefault(section, {}).update(table)
    config.write_text(render_toml(merged), encoding="utf-8")
    return original


CONFIG_JOURNAL = "config_journal.json"


def snapshot_configs(game: Path, also: list[Path]) -> dict[str, str | None]:
    """core.toml, every mod config and the files a scenario will create: their text before the scenario (None: did
    not exist). Covers [config], [mod_config] and settings changed from inside the game (set_setting steps)."""
    config = game / "sacore" / "config"
    files = [config / "core.toml", *sorted((config / "mods").glob("*.toml")), *also]
    return {str(path.resolve()): path.read_text(encoding="utf-8") if path.exists() else None for path in files}


def restore_configs(snapshot: dict[str, str | None]) -> None:
    for name, text in snapshot.items():
        path = Path(name)  # absolute: snapshot_configs resolved it
        if text is None:
            path.unlink(missing_ok=True)
        elif not path.exists() or path.read_text(encoding="utf-8") != text:
            path.write_text(text, encoding="utf-8")


def restore_journal(testing: Path) -> bool:
    """Puts back the configs of a run that was killed before it could restore them; True if there was one."""
    journal = testing / CONFIG_JOURNAL
    if not journal.exists():
        return False
    restore_configs(json.loads(journal.read_text(encoding="utf-8")))
    journal.unlink()
    return True


def run_one(game: Path, scenario: Path, extra_seconds: float) -> dict:
    import tomllib
    data = tomllib.loads(scenario.read_text(encoding="utf-8"))
    testing = game / "sacore" / "testing"
    testing.mkdir(parents=True, exist_ok=True)
    restore_journal(testing)
    config_file = game / "sacore" / "config" / "core.toml"
    mod_files = {mod_id: game / "sacore" / "config" / "mods" / f"{mod_id}.toml" for mod_id in data.get("mod_config", {})}
    snapshot = snapshot_configs(game, list(mod_files.values()))
    (testing / CONFIG_JOURNAL).write_text(json.dumps(snapshot), encoding="utf-8")
    apply_overrides(config_file, data.get("config", {}))
    # [mod_config.<mod id>] overrides go into sacore/config/mods/<mod id>.toml (flat keys).
    for mod_id, values in data.get("mod_config", {}).items():
        mod_file = mod_files[mod_id]
        mod_file.parent.mkdir(parents=True, exist_ok=True)
        original = mod_file.read_text(encoding="utf-8") if mod_file.exists() else None
        merged = tomllib.loads(original) if original else {}
        merged.update(values)
        mod_file.write_text(render_toml(merged), encoding="utf-8")
    timeout = float(data.get("timeout_seconds", 180)) + extra_seconds
    expect_crash = bool(data.get("expect_crash", False))
    results_file = testing / "results.json"
    if results_file.exists():
        results_file.unlink()
    crashes = game / "sacore" / "crashes"
    before = set(crashes.glob("*.txt")) if crashes.is_dir() else set()
    shutil.copyfile(scenario, testing / "scenario.toml")
    exe = find_exe(game)
    outcome = {"game": str(game), "scenario": scenario.stem, "passed": False, "detail": ""}
    try:
        if exe is None:
            outcome["detail"] = "no game executable"
            return outcome
        start = time.monotonic()
        process = subprocess.Popen([str(exe)], cwd=str(game), startupinfo=no_activate_startupinfo())
        sampler = MemorySampler(process.pid)
        kill_after = data.get("kill_after_seconds")
        code = None
        while code is None:
            elapsed = time.monotonic() - start
            if kill_after is not None and elapsed > float(kill_after):
                # Incomplete shutdown test: end the process the hard way, then require a clean next start.
                process.kill()
                process.wait(timeout=10)
                return run_after_kill(game, data, testing, results_file, crashes, before, exe, timeout, outcome)
            if elapsed > timeout:
                process.kill()
                process.wait(timeout=10)
                outcome["detail"] = f"game did not exit within {timeout:.0f} s (killed)"
                return outcome
            sampler.sample(elapsed)
            try:
                code = process.wait(timeout=1 if kill_after is not None else 10)
            except subprocess.TimeoutExpired:
                pass
        if kill_after is not None:
            outcome["detail"] = f"game exited by itself (exit {code & 0xFFFFFFFF:#x}) before the planned kill"
            return outcome
        outcome["memory"] = sampler.summary()
        outcome["memory_samples_mb"] = [(round(s), round(v / 2**20, 1)) for s, v in sampler.samples]
        outcome["seconds"] = round(time.monotonic() - start, 1)
        outcome["exit_code"] = code & 0xFFFFFFFF
        if expect_crash:
            new_reports = sorted(set(crashes.glob("*.txt")) - before) if crashes.is_dir() else []
            text = new_reports[-1].read_text(encoding="utf-8", errors="replace") if new_reports else ""
            ok = outcome["exit_code"] == CRASH_EXIT_CODE and "Faulting address: SACore.asi" in text
            dump_ok = bool(new_reports) and new_reports[-1].with_suffix(".dmp").is_file()
            # One package to send, with report, dump, session log and settings, readable as a zip.
            package = new_reports[-1].with_suffix(".zip") if new_reports else None
            package_names: set[str] = set()
            intact = False
            if package and package.is_file():
                import zipfile
                with zipfile.ZipFile(package) as archive:
                    intact = archive.testzip() is None  # every CRC checks out
                    package_names = set(archive.namelist())
            needed = {new_reports[-1].name, new_reports[-1].with_suffix(".dmp").name, "logs/sacore.log",
                      "config/core.toml"} if new_reports else set()
            package_ok = intact and bool(package_names) and needed <= package_names
            outcome["passed"] = ok and dump_ok and package_ok
            outcome["detail"] = (f"exit {outcome['exit_code']:#x}, report {'found' if new_reports else 'MISSING'}, "
                                 f"dump {'found' if dump_ok else 'MISSING'}, package "
                                 f"{'complete' if package_ok else 'MISSING ' + str(sorted(needed - package_names))}")
            return outcome
        if not results_file.exists():
            outcome["detail"] = f"no results.json (exit code {outcome['exit_code']:#x})"
            return outcome
        results = json.loads(results_file.read_text(encoding="utf-8"))
        outcome["results"] = results
        outcome["passed"] = bool(results.get("passed")) and outcome["exit_code"] == 0
        failed = [s for s in results.get("steps", []) if not s.get("passed")]
        outcome["detail"] = (f"{len(results.get('steps', []))} steps, exit {outcome['exit_code']:#x}"
                             + (f", first failure: step {failed[0]['index']} {failed[0]['action']}: "
                                f"{failed[0]['message']}" if failed else ""))
        # Lines written during shutdown can only be checked after the process ended.
        log_text = (game / "sacore" / "logs" / "sacore.log").read_text(encoding="utf-8", errors="replace")
        missing = [text for text in data.get("expect_log_after_exit", []) if text not in log_text]
        if missing:
            outcome["passed"] = False
            outcome["detail"] += f", missing after exit: {missing}"
        return outcome
    finally:
        scenario_file = testing / "scenario.toml"
        if scenario_file.exists():
            scenario_file.unlink()
        restore_journal(testing)


def run_after_kill(game: Path, data: dict, testing: Path, results_file: Path, crashes: Path, before: set,
                   exe: Path, timeout: float, outcome: dict) -> dict:
    """Second start after a killed session: runs the scenario's [[after_kill_step]] list and must pass."""
    new_reports = sorted(set(crashes.glob("*.txt")) - before) if crashes.is_dir() else []
    previous_log = game / "sacore" / "logs" / "sacore.log"
    killed_log = previous_log.read_text(encoding="utf-8", errors="replace") if previous_log.is_file() else ""
    if new_reports or "first frame presented" not in killed_log:
        outcome["detail"] = (f"killed session: {len(new_reports)} crash report(s), first frame "
                             f"{'logged' if 'first frame presented' in killed_log else 'NOT logged'}")
        return outcome
    if results_file.exists():
        results_file.unlink()
    steps = data.get("after_kill_step", [])
    text = f'name = "{data.get("name", "after_kill")}_restart"\n'
    for step in steps:
        text += "\n[[step]]\n" + render_toml(step)
    (testing / "scenario.toml").write_text(text, encoding="utf-8")
    start = time.monotonic()
    process = subprocess.Popen([str(exe)], cwd=str(game), startupinfo=no_activate_startupinfo())
    try:
        code = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)
        outcome["detail"] = f"restart after the kill did not finish within {timeout:.0f} s"
        return outcome
    outcome["seconds"] = round(time.monotonic() - start, 1)
    outcome["exit_code"] = code & 0xFFFFFFFF
    if not results_file.exists():
        outcome["detail"] = f"restart after the kill: no results.json (exit {outcome['exit_code']:#x})"
        return outcome
    results = json.loads(results_file.read_text(encoding="utf-8"))
    outcome["results"] = results
    outcome["passed"] = bool(results.get("passed")) and outcome["exit_code"] == 0
    failed = [s for s in results.get("steps", []) if not s.get("passed")]
    outcome["detail"] = (f"killed after {data['kill_after_seconds']} s without a crash report; restart: "
                         f"{len(results.get('steps', []))} steps, exit {outcome['exit_code']:#x}"
                         + (f", first failure: step {failed[0]['index']} {failed[0]['action']}: "
                            f"{failed[0]['message']}" if failed else ""))
    return outcome


def toml_value(v) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, list):
        return "[" + ", ".join(toml_value(x) for x in v) + "]"
    return '"' + str(v).replace("\\", "\\\\").replace('"', '\\"') + '"'


def merge_session(name: str, members: list[Path], folder: Path) -> tuple[Path, list[tuple[str, int, int]]] | None:
    """One scenario from several that share `session`: their steps in a row (one quit at the end), so the game starts
    once for all of them. Returns the merged file and each member's step range, or None when they cannot share a game
    (different [config], conflicting [mod_config] values, crash or kill scenarios)."""
    import tomllib
    datas = [tomllib.loads(m.read_text(encoding="utf-8")) for m in members]
    if any(d.get("expect_crash") or d.get("kill_after_seconds") for d in datas):
        return None
    if any(d.get("config", {}) != datas[0].get("config", {}) for d in datas):
        return None
    mod_config: dict = {}
    for d in datas:
        for mod_id, values in d.get("mod_config", {}).items():
            merged = mod_config.setdefault(mod_id, {})
            for key, value in values.items():
                if key in merged and merged[key] != value:
                    return None
                merged[key] = value
    lines = [f'name = "{name}"', f"timeout_seconds = {sum(float(d.get('timeout_seconds', 180)) for d in datas)}"]
    after_exit = [text for d in datas for text in d.get("expect_log_after_exit", [])]
    if after_exit:
        lines.append(f"expect_log_after_exit = {toml_value(after_exit)}")
    for section, table in datas[0].get("config", {}).items():
        lines.append(f"\n[config.{section}]")
        lines += [f"{k} = {toml_value(v)}" for k, v in table.items()]
    for mod_id, table in mod_config.items():
        lines.append(f"\n[mod_config.{mod_id}]")
        lines += [f"{k} = {toml_value(v)}" for k, v in table.items()]
    ranges = []
    index = 0
    for member, d in zip(members, datas):
        first = index + 1
        for step in d.get("step", []):
            if step.get("action") == "quit":
                continue
            index += 1
            lines.append("\n[[step]]")
            lines += [f"{k} = {toml_value(v)}" for k, v in step.items()]
        ranges.append((member.stem, first, index))
    lines += ["\n[[step]]", 'action = "quit"']
    folder.mkdir(parents=True, exist_ok=True)
    merged_file = folder / f"session_{name}.toml"
    merged_file.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return merged_file, ranges


def split_session(outcome: dict, ranges: list[tuple[str, int, int]]) -> list[dict]:
    """Per-member outcomes of a merged session run."""
    steps = {s.get("index"): s for s in outcome.get("results", {}).get("steps", [])}
    failed_at = next((s["index"] for s in outcome.get("results", {}).get("steps", []) if not s.get("passed")), None)
    out = []
    for name, first, last in ranges:
        member = dict(outcome, scenario=name)
        mine = [steps[i] for i in range(first, last + 1) if i in steps]
        if failed_at is not None and first <= failed_at <= last:
            bad = steps[failed_at]
            member["passed"] = False
            member["detail"] = f"step {failed_at - first + 1} {bad['action']}: {bad['message']}"
        elif failed_at is not None and failed_at < first:
            member["passed"] = False
            member["detail"] = "not run: the session stopped earlier"
        elif not outcome.get("results"):
            member["passed"] = False
            member["detail"] = outcome["detail"]
        else:
            member["passed"] = len(mine) == last - first + 1 and all(s.get("passed") for s in mine) and \
                               outcome.get("exit_code") == 0
            member["detail"] = f"{len(mine)} steps in session (exit {outcome.get('exit_code', 0):#x})"
            if not member["passed"] and outcome.get("detail", "").find("missing after exit") >= 0:
                member["detail"] = outcome["detail"]
        member["seconds"] = None
        out.append(member)
    return out


def data_is_soak(scenario: Path) -> bool:
    import tomllib
    return float(tomllib.loads(scenario.read_text(encoding="utf-8")).get("timeout_seconds", 0)) >= 300


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", action="append", required=True, type=Path)
    parser.add_argument("--scenario", action="append", required=True, type=Path,
                        help="scenario file or directory of *.toml files")
    parser.add_argument("--extra-seconds", type=float, default=60.0, help="grace time on top of the scenario timeout")
    parser.add_argument("--json-out", type=Path, default=None, help="directory for per-run result files")
    parser.add_argument("--tier", choices=("quick", "full"), default="quick",
                        help="quick (default): scenarios without tier = \"full\"; full: every scenario")
    parser.add_argument("--serial", action="store_true", help="one game at a time instead of all builds side by side")
    parser.add_argument("--no-sessions", action="store_true", help="one game start per scenario, even with `session`")
    args = parser.parse_args(argv)

    import tomllib
    scenarios: list[Path] = []
    for entry in args.scenario:
        found = sorted(entry.glob("*.toml")) if entry.is_dir() else [entry]
        # A directory run skips the long scenarios (soak, the whole story) unless --tier full; a named file always runs.
        scenarios += [s for s in found if not entry.is_dir() or args.tier == "full" or
                      tomllib.loads(s.read_text(encoding="utf-8")).get("tier", "quick") != "full"]
    games = [g.resolve() for g in args.game]
    for game in games:
        if "steamapps" in str(game).lower():
            print(f"REFUSED: {game} looks like a Steam install; use a test copy")
            return 2

    started = time.monotonic()
    keep_awake()
    lock = threading.Lock()
    instance_guard = hold_instance_name() if len(games) > 1 and not args.serial else None
    if len(games) > 1 and not args.serial and instance_guard is None:
        print("could not reserve the game's instance name: running one copy at a time")
        args.serial = True

    # Scenarios with the same `session` share one game start (ordered by `session_order`, then name).
    units: list[tuple[Path, list[tuple[str, int, int]] | None]] = []
    sessions: dict[str, list[Path]] = {}
    for scenario in scenarios:
        data = tomllib.loads(scenario.read_text(encoding="utf-8"))
        if data.get("session") and not args.no_sessions:
            sessions.setdefault(data["session"], []).append(scenario)
        else:
            units.append((scenario, None))
    temp = Path(__file__).resolve().parents[1] / "build" / "runtime-sessions"
    for name, members in sessions.items():
        members.sort(key=lambda m: (tomllib.loads(m.read_text(encoding="utf-8")).get("session_order", 50), m.stem))
        merged = merge_session(name, members, temp) if len(members) > 1 else None
        if merged:
            units.append(merged)
        else:
            units += [(m, None) for m in members]

    # Jobs: a `chain` (scenarios that build on each other's saves, ordered by `chain_order`) runs as one job on one
    # copy; every other unit is a job of its own. Copies of the same build (same exe) share that build's job queue,
    # longest first by the durations of the last run.
    durations_file = Path(__file__).resolve().parents[1] / "build" / "runtime-durations.json"
    try:
        durations: dict[str, float] = json.loads(durations_file.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        durations = {}
    chains: dict[str, list] = {}
    jobs: list[list] = []
    for unit in units:
        data = tomllib.loads(unit[0].read_text(encoding="utf-8")) if unit[1] is None else {}
        if data.get("chain"):
            chains.setdefault(data["chain"], []).append((data.get("chain_order", 50), unit[0].stem, unit))
        else:
            jobs.append([unit])
    for members in chains.values():
        jobs.append([unit for _, _, unit in sorted(members, key=lambda m: (m[0], m[1]))])
    jobs.sort(key=lambda job: -sum(durations.get(u[0].stem, 30.0) for u in job))
    builds: dict[str, list[Path]] = {}
    for game in games:
        exe = find_exe(game)
        builds.setdefault(sha256_of(exe) if exe else str(game), []).append(game)
    queues = {key: list(jobs) for key in builds}

    def run_game(game: Path) -> list[dict]:
        results = []
        key = next(k for k, copies in builds.items() if game in copies)
        while True:
            with lock:
                if not queues[key]:
                    break
                job = queues[key].pop(0)
            for scenario, ranges in job:
                results += run_unit(game, scenario, ranges)
        return results

    def run_unit(game: Path, scenario: Path, ranges) -> list[dict]:
        results = []
        outcome = run_one(game, scenario, args.extra_seconds)
        if not outcome.get("passed"):
            # Keep the session log of a failed run: the next start of this copy rotates it away.
            failures = Path(__file__).resolve().parents[1] / "build" / "runtime-failures"
            failures.mkdir(parents=True, exist_ok=True)
            log = game / "sacore" / "logs" / "sacore.log"
            if log.is_file():
                shutil.copy2(log, failures / f"{game.name}-{scenario.stem}.log")
        if isinstance(outcome.get("seconds"), (int, float)):
            with lock:
                durations[scenario.stem] = outcome["seconds"]
        parts = split_session(outcome, ranges) if ranges else [outcome]
        results += parts
        with lock:
            seconds = outcome.get("seconds")
            took = f" ({seconds:.0f} s)" if isinstance(seconds, (int, float)) else ""
            if ranges:
                print(f"[{game.name}] session {scenario.stem[8:]}{took}: {len(parts)} scenarios", flush=True)
            for part in parts:
                label = part["scenario"] if ranges else scenario.stem + took
                print(f"[{game.name}] {'PASS' if part['passed'] else 'FAIL'}  {label}: {part['detail']}", flush=True)
            if outcome.get("memory") and not ranges and data_is_soak(scenario):
                print(f"        {outcome['memory']}", flush=True)
        if args.json_out:
            args.json_out.mkdir(parents=True, exist_ok=True)
            for part in parts:
                (args.json_out / f"{game.name}-{part['scenario']}.json").write_text(json.dumps(part, indent=2),
                                                                                     encoding="utf-8")
        return results

    print(f"{len(scenarios)} scenario(s), {len(jobs)} job(s) on {len(games)} game copies of {len(builds)} build(s), "
          f"{'one after the other' if args.serial else 'side by side'}", flush=True)
    outcomes = []
    if args.serial or len(games) == 1:
        for game in games:
            outcomes += run_game(game)
    else:
        # Test copies only: the harness lets a second game instance start while a scenario is armed.
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(games)) as pool:
            for results in pool.map(run_game, games):
                outcomes += results
    durations_file.parent.mkdir(parents=True, exist_ok=True)
    durations_file.write_text(json.dumps(durations, indent=1, sort_keys=True), encoding="utf-8")
    passed = sum(o["passed"] for o in outcomes)
    print(f"\n{passed}/{len(outcomes)} runs passed in {time.monotonic() - started:.0f} s")
    return 0 if passed == len(outcomes) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
