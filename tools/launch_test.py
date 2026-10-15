"""Launch a GTA SA test copy, observe it, and close it again (dev tool, Windows only).

- Confirms the vanilla "Device Selection" dialog (multi-adapter systems) with its default choice.
- Waits for the main game window, reports timings, visible windows and selected loaded modules.
- Terminates the process after --seconds (it never touches the original Steam install).

Usage:
    python tools/launch_test.py --exe <test copy exe> [--seconds 30] [--keep-running]
"""
from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time
from pathlib import Path

user32 = ctypes.WinDLL("user32", use_last_error=True)
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
BM_CLICK = 0x00F5
WM_ACTIVATEAPP = 0x001C
WM_CLOSE = 0x0010
WM_ACTIVATE = 0x0006
WM_KILLFOCUS = 0x0008
WA_INACTIVE = 0
GAME_WINDOW_CLASS = "Grand theft auto San Andreas"
DEVICE_DIALOG_TITLE = "Device Selection"


def no_activate_startupinfo() -> subprocess.STARTUPINFO:
    """Start the game without activating its window, so a test run never takes the user's focus or mouse."""
    info = subprocess.STARTUPINFO()
    info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    info.wShowWindow = 4  # SW_SHOWNOACTIVATE
    return info


def window_text(hwnd: int) -> str:
    buffer = ctypes.create_unicode_buffer(256)
    user32.GetWindowTextW(hwnd, buffer, 256)
    return buffer.value


def window_class(hwnd: int) -> str:
    buffer = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buffer, 256)
    return buffer.value


def process_windows(pid: int) -> list[int]:
    found: list[int] = []

    def callback(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            found.append(hwnd)
        return True

    user32.EnumWindows(WNDENUMPROC(callback), 0)
    return found


def child_windows(parent: int) -> list[int]:
    found: list[int] = []

    def callback(hwnd, _):
        found.append(hwnd)
        return True

    user32.EnumChildWindows(parent, WNDENUMPROC(callback), 0)
    return found


def confirm_device_dialog(dialog: int) -> bool:
    for child in child_windows(dialog):
        if window_class(child) == "Button" and window_text(child).replace("&", "").strip().upper() == "OK":
            user32.SendMessageW(child, BM_CLICK, 0, 0)
            return True
    return False


def loaded_modules(pid: int) -> list[str]:
    script = f"(Get-Process -Id {pid} -ErrorAction SilentlyContinue).Modules | ForEach-Object {{ $_.ModuleName }}"
    result = subprocess.run(["pwsh", "-NoProfile", "-Command", script], capture_output=True, text=True)
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--keep-running", action="store_true")
    parser.add_argument("--activate-after", type=float, default=None,
                        help="show and focus the game window after N seconds (diagnostic)")
    parser.add_argument("--no-dialog-confirm", action="store_true",
                        help="leave the Device Selection dialog open so the game never creates its device; use for "
                             "runs where our windowed mode is inactive (unknown-build tests) to avoid a fullscreen "
                             "display mode switch")
    parser.add_argument("--close-gracefully", action="store_true",
                        help="end the run with WM_CLOSE and require a clean process exit")
    parser.add_argument("--deactivate-after", type=float, default=None,
                        help="post WM_ACTIVATEAPP(FALSE) to the game window after N seconds (focus-loss test)")
    args = parser.parse_args(argv)

    exe = args.exe.resolve()
    if "steamapps" in str(exe).lower():
        print("REFUSED: use a test copy, not the Steam install.")
        return 2
    start = time.monotonic()
    process = subprocess.Popen([str(exe)], cwd=str(exe.parent), startupinfo=no_activate_startupinfo())
    print(f"started pid {process.pid}: {exe}")
    dialog_confirmed_at = None
    game_window_at = None
    activated = False
    deactivated = False
    while time.monotonic() - start < args.seconds:
        if process.poll() is not None:
            print(f"process exited early with code {process.returncode} after {time.monotonic() - start:.1f}s")
            return 1
        for hwnd in process_windows(process.pid):
            title, cls = window_text(hwnd), window_class(hwnd)
            if (not args.no_dialog_confirm and dialog_confirmed_at is None and title == DEVICE_DIALOG_TITLE
                    and user32.IsWindowVisible(hwnd)):
                if confirm_device_dialog(hwnd):
                    dialog_confirmed_at = time.monotonic() - start
                    print(f"{dialog_confirmed_at:6.1f}s  confirmed '{DEVICE_DIALOG_TITLE}' dialog (default device)")
            if game_window_at is None and cls == GAME_WINDOW_CLASS and user32.IsWindowVisible(hwnd):
                game_window_at = time.monotonic() - start
                print(f"{game_window_at:6.1f}s  game window visible ('{title}')")
            if (args.activate_after is not None and not activated and cls == GAME_WINDOW_CLASS
                    and time.monotonic() - start >= args.activate_after):
                activated = True
                user32.ShowWindow(hwnd, 5)  # SW_SHOW
                ok = user32.SetForegroundWindow(hwnd)
                print(f"{time.monotonic() - start:6.1f}s  activated game window (SetForegroundWindow={bool(ok)})")
            if (args.deactivate_after is not None and not deactivated and cls == GAME_WINDOW_CLASS
                    and time.monotonic() - start >= args.deactivate_after):
                deactivated = True
                user32.PostMessageW(hwnd, WM_ACTIVATE, WA_INACTIVE, 0)
                user32.PostMessageW(hwnd, WM_KILLFOCUS, 0, 0)
                user32.PostMessageW(hwnd, WM_ACTIVATEAPP, 0, 0)
                print(f"{time.monotonic() - start:6.1f}s  posted WM_ACTIVATE(WA_INACTIVE), WM_KILLFOCUS, "
                      "WM_ACTIVATEAPP(FALSE) (simulated focus loss)")
        time.sleep(0.25)

    alive = process.poll() is None
    print(f"after {args.seconds:.0f}s: alive={alive}")
    if alive:
        interesting = [m for m in loaded_modules(process.pid)
                       if m.lower().endswith(".asi") or m.lower() in ("dinput8.dll", "vorbisfile.dll", "d3d9.dll")
                       or m.lower().startswith(("nvd3dum", "nvldumd", "atiumdag", "amdxx"))]
        print("modules of interest: " + ", ".join(interesting))
        for hwnd in process_windows(process.pid):
            cls = window_class(hwnd)
            if user32.IsWindowVisible(hwnd) or cls == GAME_WINDOW_CLASS:
                rect = wt.RECT()
                user32.GetWindowRect(hwnd, ctypes.byref(rect))
                style = user32.GetWindowLongW(hwnd, -16) & 0xFFFFFFFF
                print(f"window: class='{cls}' title='{window_text(hwnd)}' visible={bool(user32.IsWindowVisible(hwnd))} "
                      f"rect=({rect.left},{rect.top})-({rect.right},{rect.bottom}) style=0x{style:08X}")
        if args.close_gracefully:
            for hwnd in process_windows(process.pid):
                if window_class(hwnd) == GAME_WINDOW_CLASS:
                    user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
            try:
                code = process.wait(timeout=20)
                print(f"closed gracefully with exit code {code}")
            except subprocess.TimeoutExpired:
                print("did not exit within 20s after WM_CLOSE")
                process.terminate()
                process.wait(timeout=10)
                return 1
        elif not args.keep_running:
            process.terminate()
            process.wait(timeout=10)
            print("terminated")
    return 0 if alive and game_window_at is not None else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
