#!/usr/bin/env python3
"""Exercise the native appearance dialog via Linux accessibility, in a test profile."""

import json
import os
import signal
import subprocess
import tempfile
import time
from pathlib import Path

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib

try:
    from launch import installed_sandbox_helper
except ImportError:
    from scripts.launch import installed_sandbox_helper


def wait_for(callback, description, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        context = GLib.MainContext.default()
        for _ in range(128):
            if not context.pending():
                break
            context.iteration(False)
        result = callback()
        if result:
            return result
        time.sleep(0.2)
    raise RuntimeError("Timed out: " + description)


def descendants(root):
    pending = [root]
    visited = 0
    while pending and visited < 2500:
        node = pending.pop()
        visited += 1
        if node is None:
            continue
        yield node
        try:
            pending.extend(node.get_child_at_index(i) for i in range(node.get_child_count()))
        except gi.repository.GLib.Error:
            pass


def find(root, name, role=None):
    for node in descendants(root):
        try:
            if node.get_name() == name and (role is None or node.get_role() == role) and node.get_state_set().contains(Atspi.StateType.SHOWING):
                return node
        except gi.repository.GLib.Error:
            pass
    return None


def click(node):
    assert node.get_state_set().contains(Atspi.StateType.ENABLED), "Control is disabled: " + node.get_name()
    action = node.get_action_iface()
    assert action and action.get_n_actions() > 0, "Control has no accessible action"
    assert action.do_action(0), "Accessible action failed"


def select_preset(app, name):
    combo = wait_for(lambda: find(app, "Appearance preset", Atspi.Role.COMBO_BOX), "preset selector")
    click(combo)
    option = wait_for(lambda: find(app, name, Atspi.Role.MENU_ITEM), "preset option " + name)
    click(option)


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / ".local/native-smoke"
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    helper = Path("/opt/google/chrome/chrome-sandbox")
    if installed_sandbox_helper(helper):
        env.setdefault("CHROME_DEVEL_SANDBOX", str(helper))
    if env.get("CHROME_DEVEL_SANDBOX") == "":
        raise RuntimeError("Refusing an empty sandbox helper setting")
    env["CHROME_DESKTOP"] = "blynx-dev.desktop"
    report = {"checks": [], "passed": False}
    with tempfile.TemporaryDirectory(prefix="native-profile-", dir=root / ".local") as profile:
        with (output / "browser.log").open("w") as log:
            browser = subprocess.Popen([
                str(root / ".local/chromium/src/out/linux-release/chrome"),
                "--user-data-dir=" + profile, "--ozone-platform=wayland",
                "--force-renderer-accessibility", "--no-first-run", "about:blank",
            ], env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                def own_app():
                    desktop = Atspi.get_desktop(0)
                    for i in range(desktop.get_child_count()):
                        app = desktop.get_child_at_index(i)
                        if app and app.get_process_id() == browser.pid:
                            return app
                    return None

                app = wait_for(own_app, "native browser accessibility registration")
                toolbar = wait_for(lambda: find(app, "Customize Blynx", Atspi.Role.PUSH_BUTTON), "custom toolbar button")
                click(toolbar)
                field = wait_for(lambda: find(app, "Accent color (#RRGGBB)", Atspi.Role.ENTRY), "native appearance dialog")
                report["checks"].append("native toolbar opens the appearance dialog")
                select_preset(app, "Dark")
                wait_for(lambda: Atspi.Text.get_text(field, 0, -1) == "#8AB4F8", "Dark preset updates the accent field")
                vertical = wait_for(lambda: find(app, "Show tabs vertically", Atspi.Role.CHECK_BOX), "vertical tabs checkbox")
                click(vertical)
                wait_for(lambda: vertical.get_state_set().contains(Atspi.StateType.CHECKED), "vertical tab control checked")
                click(wait_for(lambda: find(app, "Apply", Atspi.Role.PUSH_BUTTON), "Apply button"))
                wait_for(lambda: not find(app, "Accent color (#RRGGBB)", Atspi.Role.ENTRY), "Apply closes the dialog")
                wait_for(lambda: find(app, "Collapse tabs", Atspi.Role.PUSH_BUTTON), "vertical tab strip is visible")
                report["checks"].append("Apply switches the actual browser to a visible vertical tab strip")

                prefs_path = Path(profile) / "Default/Preferences"
                def saved():
                    try:
                        prefs = json.loads(prefs_path.read_text())
                        value = json.loads(prefs["browser_final"]["appearance_json"])
                        return value if value["accent_color"] == "#8AB4F8" else None
                    except (OSError, ValueError, KeyError):
                        return None

                value = wait_for(saved, "appearance persisted to the test profile", seconds=45)
                assert json.loads(prefs_path.read_text())["vertical_tabs"]["enabled"] is True
                report["checks"].append("native Apply writes the chosen accent to disk")
                report["checks"].append("native vertical tabs control persists upstream tab layout")
                os.killpg(browser.pid, signal.SIGTERM)
                browser.wait(timeout=10)
                browser = subprocess.Popen([
                    str(root / ".local/chromium/src/out/linux-release/chrome"),
                    "--user-data-dir=" + profile, "--ozone-platform=wayland",
                    "--force-renderer-accessibility", "--no-first-run", "about:blank",
                ], env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                app = wait_for(own_app, "restarted browser accessibility registration")
                click(wait_for(lambda: find(app, "Customize Blynx", Atspi.Role.PUSH_BUTTON), "custom toolbar button"))
                field = wait_for(lambda: find(app, "Accent color (#RRGGBB)", Atspi.Role.ENTRY), "reopened appearance dialog")
                assert Atspi.Text.get_text(field, 0, -1) == "#8AB4F8"
                report["checks"].append("restarted browser loads the saved accent in the native dialog")
                select_preset(app, "Default")
                click(wait_for(lambda: find(app, "Cancel", Atspi.Role.PUSH_BUTTON), "Cancel button"))
                assert saved() == value
                report["checks"].append("Cancel discards a staged preset and preserves the saved configuration")
                report["passed"] = True
            except Exception as error:
                report["error"] = str(error)
                if "app" in locals():
                    report["native_controls"] = []
                    for node in descendants(app):
                        try:
                            name = node.get_name()
                            if name and node.get_role() in (Atspi.Role.COMBO_BOX, Atspi.Role.MENU_ITEM, Atspi.Role.LIST_ITEM, Atspi.Role.ENTRY):
                                report["native_controls"].append({"name": name, "role": node.get_role_name(), "interfaces": list(node.get_interfaces())})
                            if name == "Apply":
                                report.setdefault("apply_controls", []).append({"showing": node.get_state_set().contains(Atspi.StateType.SHOWING), "actions": [node.get_action_name(i) for i in range(node.get_n_actions())]})
                            if name and any(text in name for text in ("unavailable", "valid RGB", "unsupported", "managed", "not saved", "profile", "Preview")):
                                report.setdefault("native_status", []).append(name)
                        except Exception:
                            pass
                if "prefs_path" in locals() and prefs_path.is_file():
                    try:
                        prefs = json.loads(prefs_path.read_text())
                        report["test_appearance_prefs"] = prefs.get("browser_final", {})
                        report["test_vertical_prefs"] = prefs.get("vertical_tabs", {})
                    except (OSError, ValueError):
                        pass
            finally:
                if browser.poll() is None:
                    os.killpg(browser.pid, signal.SIGTERM)
                    try:
                        browser.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        os.killpg(browser.pid, signal.SIGKILL)
                        browser.wait()
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
