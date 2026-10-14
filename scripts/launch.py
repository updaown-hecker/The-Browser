#!/usr/bin/env python3
"""Launch the workspace's native browser with its own desktop identity."""

from __future__ import annotations

import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path

try:
    from build import main as build_main
except ImportError:
    from scripts.build import main as build_main


def installed_sandbox_helper(path: Path) -> bool:
    """Accept only an installed, root-owned, non-writable SUID executable."""
    try:
        info = path.stat()
        return (
            stat.S_ISREG(info.st_mode)
            and info.st_uid == 0
            and bool(info.st_mode & stat.S_ISUID)
            and bool(info.st_mode & stat.S_IXOTH)
            and not info.st_mode & (stat.S_IWGRP | stat.S_IWOTH)
            and os.access(path, os.X_OK)
        )
    except OSError:
        return False


def main(argv=None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if sys.platform.startswith("linux"):
        os.environ["CHROME_DESKTOP"] = "blynx-dev.desktop"
        helper = Path("/opt/google/chrome/chrome-sandbox")
        if "CHROME_DEVEL_SANDBOX" not in os.environ and installed_sandbox_helper(helper):
            os.environ["CHROME_DEVEL_SANDBOX"] = str(helper)
        elif os.environ.get("CHROME_DEVEL_SANDBOX") == "":
            print("launch.py: refusing an empty CHROME_DEVEL_SANDBOX (disables the SUID sandbox).", file=sys.stderr)
            return 2
        if os.environ.get("WAYLAND_DISPLAY") and not any(
            value.split("=", 1)[0] == "--ozone-platform" for value in arguments
        ):
            arguments.insert(0, "--ozone-platform=wayland")
    result = build_main(["run", "--", *arguments])
    # KDE application-menu launches have no terminal to display a start error.
    if result and (os.environ.get("DESKTOP_STARTUP_ID") or os.environ.get("XDG_ACTIVATION_TOKEN")):
        notifier = shutil.which("notify-send")
        if notifier:
            try:
                subprocess.run([
                    notifier, "--app-name=Blynx", "Blynx could not start",
                    "The development build may be incomplete. Run ./blynx in the project terminal to see the exact error.",
                ], check=False, timeout=3)
            except (OSError, subprocess.TimeoutExpired):
                pass
    return result


if __name__ == "__main__":
    raise SystemExit(main())
