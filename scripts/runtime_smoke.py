#!/usr/bin/env python3
"""Exercise the built native browser with local fixtures and an isolated profile."""

import http.server
import os
import signal
import subprocess
import tempfile
import threading
import time
from pathlib import Path

try:
    from launch import installed_sandbox_helper
except ImportError:
    from scripts.launch import installed_sandbox_helper


class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/download":
            body = b"Browser Final download passed\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Disposition", 'attachment; filename="smoke.txt"')
        else:
            body = (b"<title>Second local page</title><p>Navigation passed</p>" if self.path == "/next" else
                    b'<title>Browser Final local smoke</title><p id="result"></p>'
                    b'<script>document.querySelector("#result").textContent="JavaScript executed"</script>'
                    b'<a id="next" href="/next">Next</a><a id="download" href="/download">Download</a>')
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / ".local/runtime-smoke"
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    helper = Path("/opt/google/chrome/chrome-sandbox")
    if installed_sandbox_helper(helper):
        env.setdefault("CHROME_DEVEL_SANDBOX", str(helper))
    if env.get("CHROME_DEVEL_SANDBOX") == "":
        raise RuntimeError("Refusing an empty sandbox helper setting")
    source = root / ".local/chromium/src"
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Fixture)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="runtime-profile-", dir=root / ".local") as profile:
        with (output / "browser.log").open("w") as log:
            browser = subprocess.Popen([
                str(source / "out/linux-release/chrome"), "--user-data-dir=" + profile,
                "--headless", "--no-first-run", "--remote-debugging-port=0",
                "--load-extension=" + str(root / "browser/tests/runtime_extension"),
                "--disable-background-networking", "about:blank",
            ], env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                active_port = Path(profile) / "DevToolsActivePort"
                deadline = time.monotonic() + 30
                while not active_port.exists():
                    if browser.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("Browser did not expose its isolated DevTools port")
                    time.sleep(0.1)
                port = active_port.read_text().splitlines()[0]
                node = source / "third_party/node/linux/node-linux-x64/bin/node"
                result = subprocess.run([
                    str(node), str(root / "tests/runtime_smoke.mjs"), port,
                    "http://127.0.0.1:" + str(server.server_port) + "/", str(output),
                ], timeout=150, check=False)
                return result.returncode
            finally:
                if browser.poll() is None:
                    os.killpg(browser.pid, signal.SIGTERM)
                    try:
                        browser.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        os.killpg(browser.pid, signal.SIGKILL)
                        browser.wait()
                server.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
