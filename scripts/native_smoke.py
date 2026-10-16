#!/usr/bin/env python3
"""Run page/Settings smoke natively, then inspect actual browser chrome."""
import json
from collections import deque
import time
import urllib.parse
import urllib.request
import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib


def descendants(root):
    pending = deque([root])
    visited = 0
    while pending and visited < 2500:
        node = pending.popleft()
        visited += 1
        if node is None:
            continue
        yield node
        try:
            pending.extend(node.get_child_at_index(i) for i in range(node.get_child_count()))
        except GLib.Error:
            pass


def check_native_chrome(pid, output, port, platform="wayland"):
    report_path = output / "results.json"
    report = json.loads(report_path.read_text())
    report.update(passed=False, error="Native chrome checks did not complete")
    report_path.write_text(json.dumps(report, indent=2) + "\n")
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
                            check_command_center(app, report, port, pid, platform)
                            report["passed"] = True
                            report.pop("error", None)
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


def check_command_center(app, report, port, pid, platform):
    """Exercise browser-local search through scoped X11 key and pointer events."""
    def visible():
        app.clear_cache()
        context = GLib.MainContext.default()
        for _ in range(128):
            if not context.pending():
                break
            context.iteration(False)
        return [node for node in descendants(app)
                if node.get_state_set().contains(Atspi.StateType.SHOWING)]

    def wait_for(description, predicate):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                result = predicate(visible())
                if result:
                    return result
            except GLib.Error:
                pass
            time.sleep(.1)
        raise RuntimeError("Native Blynx search timed out: " + description)

    def dialog(nodes):
        return next((node for node in nodes if node.get_role() == Atspi.Role.DIALOG and
                     node.get_name() == "Blynx search"), None)

    def controls(nodes):
        root = dialog(nodes)
        return [node for node in descendants(root)
                if node.get_state_set().contains(Atspi.StateType.SHOWING)] if root else []

    # Parent page controls must not be mistaken for similarly named native ones.
    def field():
        return wait_for("focused search field", lambda nodes: next(
            (node for node in controls(nodes) if node.get_name() == "Search or enter an address" and
             node.get_state_set().contains(Atspi.StateType.FOCUSED)), None))

    assert not any(node.get_name() in ("Blynx command center", "Blynx search") and
                   node.get_role() == Atspi.Role.PUSH_BUTTON for node in visible()), "Removed toolbar button is visible"
    report["checks"].append("native toolbar has no Blynx search button")
    if platform == "wayland":
        report.setdefault("limitations", []).append(
            "Ctrl+Space, typing, Enter and Escape run separately under X11; "
            "Chromium's AT-SPI text setter and Wayland key synthesis are unavailable here.")
        return

    from native_x11_input import NativeX11Input
    def key(symbol, control=False):
        NativeX11Input(pid, "Blynx search").press_key(symbol, control)
    def open_search():
        key(0x20, True)
        return field()
    def search(text):
        target = field()
        NativeX11Input(pid, "Blynx search").replace(text)
        wait_for("typed input", lambda _nodes: Atspi.Text.get_text(target, 0, -1) == text)
    def close_search():
        key(0xff1b)
        wait_for("Escape closes popup", lambda nodes: not dialog(nodes))
    def enter():
        key(0xff0d)
        wait_for("accepted popup closes", lambda nodes: not dialog(nodes))
    def address_contains(fragment):
        wait_for("address " + fragment, lambda nodes: next(
            (node for node in nodes if node.get_name() == "Address and search bar" and
             fragment in Atspi.Text.get_text(node, 0, -1)), None))

    def click_address():
        address = next(node for node in visible() if node.get_name() == "Address and search bar")
        point = Atspi.Component.get_extents(address, Atspi.CoordType.WINDOW)
        NativeX11Input(pid, "Blynx search").click(point.x + point.width // 2, point.y + point.height // 2)
        wait_for("address receives outside click", lambda nodes: any(
            node.get_name() == "Address and search bar" and
            node.get_state_set().contains(Atspi.StateType.FOCUSED) for node in nodes))

    open_search()
    click_address()
    wait_for("outside click dismisses popover", lambda nodes: not dialog(nodes))
    report["checks"].append("clicking outside search dismisses it and focuses the browser address field")

    target = open_search()
    assert Atspi.Text.get_text(target, 0, -1) == ""
    names = [node.get_name() for node in controls(visible())]
    assert "Go" not in names and "Cancel" not in names
    assert "Focus address bar" in names and "Open downloads" in names
    search("unmatched-search-fixture-472901")
    wait_for("web search fallback", lambda nodes: any(
        node.get_name().startswith("Search or open address · unmatched-search") for node in controls(nodes)))
    close_search()
    address_contains("blynx://sandbox")
    report["checks"].append("Ctrl+Space focuses a clean popup; unmatched input offers omnibox routing and Escape leaves the page unchanged")

    target = open_search()
    assert Atspi.Text.get_text(target, 0, -1) == "", "Reopen retained a cancelled query"
    search("OPEN DOWNLOADS")
    enter()
    outcome = wait_for("upstream Downloads outcome", lambda nodes:
        next(("page" for node in nodes if node.get_name() == "Address and search bar" and
              "blynx://downloads" in Atspi.Text.get_text(node, 0, -1)), None) or
        next(("bubble" for node in nodes if node.get_name() == "Full download history"), None))
    if outcome == "bubble":
        key(0xff1b)
    report["checks"].append("case-insensitive tool search and Enter execute upstream Downloads after popup dismissal")

    # Use only the owned HTTP fixture; no external search provider is contacted.
    fixture = report["expectedBookmark"]["url"] + "next"
    open_search()
    search(fixture)
    enter()
    address_contains(fixture.removeprefix("http://"))
    report["checks"].append("popup address entry navigates through Chromium's real omnibox classifier")
    click_address()
    open_search()
    close_search()
    click_address()
    report["checks"].append("browser receives pointer input after Enter navigation and after reopening/dismissing search")


    open_search()
    search("Settings - Appearance")
    enter()
    address_contains("blynx://settings/appearance")
    report["checks"].append("popup tab search activates the existing Settings tab")

    open_search()
    enter()  # Default tool is Focus address bar.
    wait_for("top address bar focus", lambda nodes: next(
        (node for node in nodes if node.get_name() == "Address and search bar" and
         node.get_state_set().contains(Atspi.StateType.FOCUSED)), None))
    open_search()
    close_search()
    report["checks"].append("Focus address bar hands off to the real omnibox and Ctrl+Space also works while it is focused")

    for vertical in (False, True):
        open_search()
        search("Switch horizontal / vertical tabs")
        enter()
        wait_for("tab layout=" + str(vertical), lambda nodes:
                 any(node.get_name() == "Collapse tabs" for node in nodes) == vertical)
    report["checks"].append("popup tool search toggles the native tab layout and restores it")

    endpoint = "http://127.0.0.1:" + str(port)
    fixture = "data:text/html,<title>Blynx stale tab fixture 472901</title>"
    request = urllib.request.Request(endpoint + "/json/new?" + urllib.parse.quote(fixture), method="PUT")
    with urllib.request.urlopen(request, timeout=5) as response:
        target = json.load(response)
    try:
        def loaded(_nodes):
            with urllib.request.urlopen(endpoint + "/json/list", timeout=5) as response:
                return any(item.get("id") == target["id"] and item.get("title") == "Blynx stale tab fixture 472901"
                           for item in json.load(response))
        wait_for("stale-tab fixture", loaded)
        open_search()
        search("Blynx stale tab fixture 472901")
        with urllib.request.urlopen(endpoint + "/json/close/" + target["id"], timeout=5) as response:
            response.read()
        def closed(_nodes):
            with urllib.request.urlopen(endpoint + "/json/list", timeout=5) as response:
                return all(item.get("id") != target["id"] for item in json.load(response))
        wait_for("fixture tab closes", closed)
        key(0xff0d)
        field()
        wait_for("stale result removed", lambda nodes: not any(
            node.get_name().startswith("Switch to tab · Blynx stale") for node in controls(nodes)))
        close_search()
        report["checks"].append("a closed tab refreshes results without activating another tab or searching the web")
    finally:
        try:
            with urllib.request.urlopen(endpoint + "/json/list", timeout=5) as response:
                remaining = json.load(response)
            if any(item.get("id") == target["id"] for item in remaining):
                urllib.request.urlopen(endpoint + "/json/close/" + target["id"], timeout=5).close()
        except (OSError, ValueError):
            pass


if __name__ == "__main__":
    import argparse
    from runtime_smoke import main
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("wayland", "x11"), default="wayland",
                        help="X11 also runs scoped native keyboard command-center checks")
    args = parser.parse_args()
    raise SystemExit(main(native=True, native_platform=args.platform))
