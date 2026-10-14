#!/usr/bin/env python3
"""Install this development checkout's user-local Linux application launcher.

Does not install the browser, set defaults, modify OS policy, or copy profiles.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path


def desktop_quote(value: str) -> str:
    if "\n" in value or "\r" in value:
        raise ValueError("Desktop paths cannot contain newlines.")
    # Exec's quoted arguments have an additional desktop-entry escape layer.
    value = value.replace("\\", "\\\\\\\\")
    for character in ('"', '`', '$'):
        value = value.replace(character, "\\\\" + character)
    return '"' + value.replace("%", "%%") + '"'


def main() -> int:
    if not sys.platform.startswith("linux"):
        print("This development launcher is Linux-only.", file=sys.stderr)
        return 2
    root = Path(__file__).resolve().parents[1]
    launcher = root / "blynx"
    icon = root / "browser/resources/icons/blynx-taskbar.png"
    if not launcher.is_file() or not os.access(launcher, os.X_OK):
        raise ValueError("The workspace blynx launcher is missing or not executable.")
    applications = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "applications"
    applications.mkdir(parents=True, exist_ok=True)
    target = applications / "blynx-dev.desktop"
    marker = "X-Blynx-Development=true"
    if target.is_symlink() or (target.exists() and marker not in target.read_text()):
        raise ValueError("Refusing to replace an unrelated or symlinked desktop entry.")
    data = "\n".join([
        "[Desktop Entry]", "Type=Application", "Version=1.0",
        "Name=Blynx", "GenericName=Web Browser",
        "Comment=Native Chromium-based development browser",
        "Exec=" + desktop_quote(str(launcher)) + " %U",
        "Icon=" + str(icon), "Terminal=false", "Categories=Network;WebBrowser;",
        "StartupNotify=true", "StartupWMClass=blynx-dev", marker, "",
    ])
    target.write_text(data, encoding="utf-8")
    # Keep existing pinned shortcuts working without a duplicate menu entry.
    legacy = applications / "browser-final-dev.desktop"
    if legacy.is_file() and not legacy.is_symlink() and "X-BrowserFinal-Development=true" in legacy.read_text():
        legacy.write_text(data + "NoDisplay=true\nX-BrowserFinal-Development=true\n", encoding="utf-8")
    print("Installed application menu entry:", target)
    print("Launch from the application menu or:", launcher)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print("install_launcher.py:", error, file=sys.stderr)
        raise SystemExit(2)
