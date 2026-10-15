#!/usr/bin/env python3
"""Run page/Settings smoke in native Wayland, then inspect actual browser chrome."""
import json
import time
import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib


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
        except GLib.Error:
            pass


def check_native_chrome(pid, output):
    report_path = output / "results.json"
    report = json.loads(report_path.read_text())
    deadline = time.monotonic() + 30
    try:
        while time.monotonic() < deadline:
            context = GLib.MainContext.default()
            for _ in range(128):
                if not context.pending():
                    break
                context.iteration(False)
            try:
                desktop = Atspi.get_desktop(0)
                apps = [desktop.get_child_at_index(i) for i in range(desktop.get_child_count())]
                app = next((app for app in apps if app and app.get_process_id() == pid), None)
                if app:
                    visible = [node for node in descendants(app) if node.get_state_set().contains(Atspi.StateType.SHOWING)]
                    names = [node.get_name() for node in visible]
                    address = next((node for node in visible if node.get_name() == "Address and search bar"), None)
                    if address and "Collapse tabs" in names:
                        text = Atspi.Text.get_text(address, 0, -1)
                        if "blynx://sandbox" in text:
                            assert "Customize Blynx" not in names, "Removed popup toolbar control is still visible"
                            report["checks"].extend([
                                "native address bar displays blynx://sandbox after navigating to chrome://sandbox",
                                "Settings enables the actual visible native vertical tab strip",
                                "native toolbar has no Customize Blynx popup button",
                            ])
                            report["visible_address"] = text
                            return
            except GLib.Error:
                # Windows/tabs can disappear during an accessibility snapshot.
                # Retry a fresh tree; missing required chrome still times out.
                time.sleep(.2)
                continue
            time.sleep(.2)
        raise RuntimeError("Native alias address/vertical-strip accessibility assertion timed out")
    except Exception as error:
        report["passed"] = False
        report["error"] = str(error)
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    from runtime_smoke import main
    raise SystemExit(main(native=True))
