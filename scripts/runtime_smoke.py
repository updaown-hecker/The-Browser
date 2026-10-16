#!/usr/bin/env python3
"""Exercise the built native browser with local fixtures and an isolated profile."""

import http.server
import json
import os
import signal
import subprocess
import tempfile
import threading
import time
from pathlib import Path

try:
    from launch import installed_sandbox_helper
    from build import _require_integration, _require_completed_browser_build
    from bootstrap import resolve_paths
except ImportError:
    from scripts.launch import installed_sandbox_helper
    from scripts.build import _require_integration, _require_completed_browser_build
    from scripts.bootstrap import resolve_paths


class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/download":
            body = b"Blynx download passed\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Disposition", 'attachment; filename="smoke.txt"')
        else:
            body = (b"<title>Second local page</title><p>Navigation passed</p>" if self.path == "/next" else
                    b'<title>Blynx local smoke</title><p id="result"></p>'
                    b'<script>document.querySelector("#result").textContent="JavaScript executed"</script>'
                    b'<a id="next" href="/next">Next</a><a id="download" href="/download">Download</a>')
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


def main(native=False, native_platform="wayland"):
    root = Path(__file__).resolve().parents[1]
    if native_platform not in ("wayland", "x11"):
        raise ValueError("Unsupported native smoke platform")
    output = root / ((".local/native-smoke-x11" if native_platform == "x11" else
                      ".local/native-smoke") if native else ".local/runtime-smoke")
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / "results.json"
    report_path.write_text(json.dumps({
        "checks": [], "pageErrors": [], "passed": False,
        "error": "Smoke did not complete",
    }) + "\n")
    source = root / ".local/chromium/src"
    # Smoke must not claim success using an old executable / partially built
    # resources. Match the development launcher's current-integration guards.
    try:
        paths = resolve_paths(root, source_root=str(source),
                              build_root=str(source / "out/linux-release"),
                              platform_name="linux")
        _require_integration(paths)
        _require_completed_browser_build(paths)
    except Exception as error:
        report = json.loads(report_path.read_text())
        report["error"] = str(error)
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        raise
    env = os.environ.copy()
    helper = Path("/opt/google/chrome/chrome-sandbox")
    if installed_sandbox_helper(helper):
        env.setdefault("CHROME_DEVEL_SANDBOX", str(helper))
    if env.get("CHROME_DEVEL_SANDBOX") == "":
        raise RuntimeError("Refusing an empty sandbox helper setting")
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Fixture)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="runtime-profile-", dir=root / ".local") as profile:
        with (output / "browser.log").open("w") as log:
            command = [
                str(source / "out/linux-release/chrome"), "--user-data-dir=" + profile,
                *( ["--ozone-platform=" + native_platform, "--force-renderer-accessibility"] if native else ["--headless"] ),
                "--no-first-run", "--remote-debugging-port=0",
                "--remote-debugging-address=127.0.0.1",
                "--load-extension=" + str(root / "browser/tests/runtime_extension"),
                "--disable-background-networking", "about:blank",
            ]
            browser = None
            try:
                for phase in ("initial", "restart", "completed"):
                    active_port = Path(profile) / "DevToolsActivePort"
                    active_port.unlink(missing_ok=True)
                    browser = subprocess.Popen(command, env=env, stdout=log,
                                               stderr=subprocess.STDOUT,
                                               start_new_session=True)
                    deadline = time.monotonic() + 30
                    while not active_port.exists():
                        if browser.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError("Browser did not expose its isolated DevTools port")
                        time.sleep(0.1)
                    port = active_port.read_text().splitlines()[0]
                    node = source / "third_party/node/linux/node-linux-x64/bin/node"
                    result = subprocess.run([
                        str(node), str(root / "tests/runtime_smoke.mjs"), port,
                        "http://127.0.0.1:" + str(server.server_port) + "/",
                        str(output), phase,
                    ], timeout=210, check=False)
                    if result.returncode:
                        return result.returncode
                    if phase != "completed":
                        if native and phase == "initial":
                            from native_smoke import check_native_chrome
                            check_native_chrome(browser.pid, output, port, native_platform)
                        # Chromium's POSIX SIGINT handler performs normal UI
                        # shutdown. Restart only after the exact process exits;
                        # no forced kill can count as persistence validation.
                        report = json.loads(report_path.read_text())
                        report.update(passed=False, error="Restart verification did not complete")
                        report_path.write_text(json.dumps(report, indent=2) + "\n")
                        browser.send_signal(signal.SIGINT)
                        if browser.wait(timeout=15) != 0:
                            raise RuntimeError("Browser did not shut down cleanly before restart")
                return 0
            except Exception as error:
                report = json.loads(report_path.read_text())
                report.update(passed=False, error=str(error))
                report_path.write_text(json.dumps(report, indent=2) + "\n")
                raise
            finally:
                if browser is not None and browser.poll() is None:
                    os.killpg(browser.pid, signal.SIGTERM)
                    try:
                        browser.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        os.killpg(browser.pid, signal.SIGKILL)
                        browser.wait()
                server.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
