#!/usr/bin/env python3
"""Native X11 assistant integration checks; disposable profile and local models only.

The default run uses deterministic Ollama/API-compatible HTTP fixtures. No real
API key, remote provider, model download or personal browser profile is used.
"""
import http.server
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import threading
import time
import urllib.request

from bootstrap import resolve_paths
from build import _require_integration, _require_completed_browser_build
from launch import installed_sandbox_helper
from native_smoke import descendants
from native_x11_input import NativeX11Input


class ModelFixture(http.server.BaseHTTPRequestHandler):
    requests = []

    def log_message(self, *_args):
        pass

    def respond(self, status, value):
        body = json.dumps(value).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        if self.path == '/api/tags':
            self.respond(200, {'models': [
                {'name': 'qwen2.5:0.5b'},
                {'name': 'fixture-model'},
            ]})
            return
        body = (b'<title>Assistant fixture</title><p>Fixture article: amber owls.</p>'
                b'<input type="password" value="fixture-password-do-not-share">')
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append((self.path, request, self.headers.get('Authorization')))
        user = json.loads(request['messages'][-1]['content'])
        prompt = user['request']
        reply = {'message': 'Fixture reply', 'actions': []}
        if prompt == 'plan':
            reply = {'message': 'Opening the fixture tab.', 'actions': [
                {'type': 'open_tab', 'url': f'http://127.0.0.1:{self.server.server_port}/opened'}]}
        elif prompt == 'long':
            reply['message'] = 'Long reply start. ' + ('A complete chat message stays readable at any panel width.\n' * 30) + 'Long reply end.'
        elif prompt == 'followup':
            assert any('hello' in x['content'] for x in request['messages'][:-1])
            reply['message'] = 'Conversation history received'
        elif prompt == 'settings':
            reply = {'message': 'Updating your appearance.', 'actions': [
                {'type': 'set_theme', 'theme': 'dark'},
                {'type': 'set_vertical_tabs', 'enabled': False}]}
        elif prompt == 'invalid':
            reply = {'message': 'Unsafe proposal', 'actions': [
                {'type': 'open_tab', 'url': 'javascript:alert(1)'}]}
        elif prompt == 'page':
            assert 'amber owls' in user['browser_context']['page_text']
            reply['message'] = 'Fixture page received'
        elif prompt in ('stale', 'close'):
            shared = user['browser_context']['tabs']
            target = shared[0] if prompt == 'stale' else shared[-1]
            if prompt == 'stale':
                time.sleep(2)
            reply = {'message': 'Closing this tab.', 'actions': [
                {'type': 'close_tab', 'tab': target['tab']}]}
        elif prompt == 'tabs':
            assert user['browser_context']['tabs']
            reply = {'message': 'Switching tabs.', 'actions': [
                {'type': 'activate_tab', 'tab': user['browser_context']['tabs'][0]['tab']}]}
        elif prompt == 'slow':
            time.sleep(4)
            reply['message'] = 'Cancelled response must not appear'
        if self.path == '/redirect':
            self.send_response(302)
            self.send_header('Location', '/credential-sink')
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        text = json.dumps(reply)
        self.respond(200, {'choices': [{'message': {'content': text}}]} if self.path != '/api/chat'
                     else {'message': {'role': 'assistant', 'content': text}, 'done': True})


def main():
    from gi.repository import Atspi, GLib
    root = Path(__file__).resolve().parents[1]
    source = root / '.local/chromium/src'
    output = root / '.local/assistant-smoke'
    output.mkdir(exist_ok=True)
    report = {'passed': False, 'checks': []}
    report_path = output / 'results.json'
    report_path.write_text(json.dumps(report))
    paths = resolve_paths(root, source_root=str(source), build_root=str(source/'out/linux-release'), platform_name='linux')
    _require_integration(paths)
    _require_completed_browser_build(paths)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), ModelFixture)
    ModelFixture.requests = []
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    try:
        with tempfile.TemporaryDirectory(prefix='assistant-test-', dir=root/'.local') as profile:
            default = Path(profile)/'Default'
            default.mkdir()
            (default/'Preferences').write_text(json.dumps({'blynx': {'onboarding_step': 4, 'assistant_config': {'version': 1, 'enabled': False, 'provider': 'ollama', 'endpoint': base+'/api/chat', 'model': 'qwen2.5:0.5b'}}}))
            env = os.environ.copy()
            helper = Path('/opt/google/chrome/chrome-sandbox')
            if installed_sandbox_helper(helper):
                env.setdefault('CHROME_DEVEL_SANDBOX', str(helper))
            with (output/'browser.log').open('w') as log:
                command = [str(source/'out/linux-release/chrome'), '--user-data-dir='+profile,
                    '--ozone-platform=x11', '--no-first-run', '--remote-debugging-port=0',
                    '--remote-debugging-address=127.0.0.1', '--force-renderer-accessibility',
                    '--disable-background-networking', base+'/article', 'about:blank']
                browser = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    def nodes():
                        context = GLib.MainContext.default()
                        for _ in range(128):
                            if not context.pending(): break
                            context.iteration(False)
                        desktop = Atspi.get_desktop(0)
                        apps = [desktop.get_child_at_index(i) for i in range(desktop.get_child_count())]
                        app = next((a for a in apps if a and a.get_process_id() == browser.pid), None)
                        if app: app.clear_cache()
                        return [n for n in descendants(app) if n.get_state_set().contains(Atspi.StateType.SHOWING)] if app else []

                    def wait(name, condition, timeout=20):
                        end = time.monotonic()+timeout
                        while time.monotonic() < end:
                            try:
                                value = condition(nodes())
                                if value: return value
                            except GLib.Error: pass
                            if browser.poll() is not None: raise RuntimeError('Browser exited')
                            time.sleep(.1)
                        subprocess.run(['import', '-window', hex(driver.window), str(output/'failure.png')], timeout=5, capture_output=True)
                        raise RuntimeError('Missing '+name)

                    def panel(ns):
                        title = next((n for n in ns if n.get_name() == 'Blynx assistant'), None)
                        return title.get_parent().get_parent() if title else None

                    def find(name):
                        def matches(ns):
                            if not panel(ns):
                                return None
                            candidates = [n for n in descendants(panel(ns))
                                          if n.get_name() == name and
                                          n.get_state_set().contains(Atspi.StateType.SHOWING)]
                            editable = [n for n in candidates if n.get_role() in
                                        (Atspi.Role.ENTRY, Atspi.Role.PASSWORD_TEXT,
                                         Atspi.Role.TEXT)]
                            focusable = [n for n in candidates if
                                         n.get_state_set().contains(Atspi.StateType.FOCUSABLE)]
                            return (editable or focusable or candidates or [None])[0]
                        n = wait(name, matches)
                        if not n.get_state_set().contains(Atspi.StateType.SHOWING):
                            try:
                                Atspi.Component.scroll_to(n, Atspi.ScrollType.ANYWHERE)
                            except GLib.Error:
                                pass
                        return n

                    def press(symbol, **modifiers):
                        for attempt in range(4):
                            try:
                                return driver.press_key(symbol, **modifiers)
                            except RuntimeError as error:
                                if 'without test-client focus' not in str(error) or attempt == 3:
                                    raise
                                time.sleep(.15)

                    def click(name):
                        n = find(name)
                        assert Atspi.Action.do_action(n, 0), name
                        time.sleep(.15)

                    def text(name, value):
                        n = find(name)
                        if not Atspi.Component.grab_focus(n):
                            bounds = Atspi.Component.get_extents(
                                n, Atspi.CoordType.WINDOW)
                            driver.click(bounds.x + bounds.width // 2,
                                         bounds.y + bounds.height // 2)
                        for _ in range(3):
                            driver.replace(value)
                            time.sleep(.1)
                            if Atspi.Text.get_text(n, 0, -1) == value:
                                break
                            bounds = Atspi.Component.get_extents(
                                n, Atspi.CoordType.WINDOW)
                            driver.click(bounds.x + bounds.width // 2,
                                         bounds.y + bounds.height // 2)
                        if name != 'API key (session only)':
                            wait('field '+name, lambda _: Atspi.Text.get_text(n, 0, -1) == value)

                    def shown(text):
                        return wait(text, lambda ns: any(text in n.get_name() for n in descendants(panel(ns))) if panel(ns) else False)

                    def send(prompt):
                        wait('ready to send', lambda _: find('Send').get_state_set().contains(Atspi.StateType.ENABLED))
                        text('Ask me anything…', prompt)
                        if prompt == 'followup':
                            press(0xff0d)
                        else:
                            click('Send')

                    def open_settings():
                        visible_settings = any(
                            n.get_name() == 'Turn on Blynx assistant' and
                            n.get_state_set().contains(Atspi.StateType.SHOWING)
                            for n in descendants(panel(nodes())))
                        if not visible_settings:
                            click('Settings')
                        find('Turn on Blynx assistant')

                    wait('owned window', lambda ns: any(n.get_name() == 'Address and search bar' for n in ns))
                    driver = NativeX11Input(browser.pid, 'Assistant fixture')
                    press(ord('a'), control=True, alt=True)
                    find('Blynx assistant')
                    time.sleep(.5)
                    subprocess.run(['import', '-window', hex(driver.window), str(output/'welcome.png')], check=True, timeout=5, capture_output=True)
                    assert not ModelFixture.requests
                    report['checks'].append('dedicated shortcut opens a docked panel, disabled without requests')
                    open_settings()
                    shown('Models updated')
                    text('Endpoint URL', base+'/api/chat')
                    click('Turn on Blynx assistant')
                    send('hello')
                    shown('Fixture reply')
                    first = ModelFixture.requests[-1]
                    assert first[0] == '/api/chat' and first[2] is None
                    assert first[1]['stream'] is False and first[1]['keep_alive'] == 0
                    context = json.loads(first[1]['messages'][-1]['content'])['browser_context']
                    assert context == {}
                    report['checks'].append('Ollama chat, no credential, no default page/tab sharing, unload flag')
                    text('Ask me anything…', 'draft')
                    request_count = len(ModelFixture.requests)
                    press(0xff0d, shift=True)
                    wait('multiline draft', lambda _: '\n' in Atspi.Text.get_text(find('Ask me anything…'), 0, -1))
                    assert len(ModelFixture.requests) == request_count
                    report['checks'].append('Shift+Enter adds a newline without submitting')
                    send('followup')
                    shown('Conversation history received')
                    time.sleep(.2)
                    subprocess.run(['import', '-window', hex(driver.window), str(output/'conversation.png')], check=True, timeout=5, capture_output=True)
                    send('long')
                    shown('Long reply end.')
                    long_node = next(n for n in descendants(panel(nodes())) if 'Long reply start.' in n.get_name())
                    print('long-reply-height', Atspi.Component.get_extents(long_node, Atspi.CoordType.WINDOW).height, flush=True)
                    subprocess.run(['import', '-window', hex(driver.window), str(output/'long-reply.png')], check=True, timeout=5, capture_output=True)
                    assert Atspi.Component.get_extents(long_node, Atspi.CoordType.WINDOW).height > 400
                    wait('latest reply scrolled into view', lambda _: (lambda r, c: r.y + r.height <= c.y)(Atspi.Component.get_extents(long_node, Atspi.CoordType.WINDOW), Atspi.Component.get_extents(find('Ask me anything…'), Atspi.CoordType.WINDOW)))
                    report['checks'].append('multi-turn history and content-sized long reply without fixed-height clipping')
                    time.sleep(.4)
                    subprocess.run(['import', '-window', hex(driver.window), str(output/'chat.png')], check=True, timeout=5, capture_output=True)
                    send('plan')
                    port = (Path(profile)/'DevToolsActivePort').read_text().splitlines()[0]
                    def pages():
                        with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list') as r:
                            return json.load(r)
                    wait('direct navigation', lambda _: any('/opened' in x['url'] for x in pages()))
                    assert not any(n.get_name() == 'Run these actions' for n in nodes())
                    report['checks'].append('requested tab opens directly without a second confirmation')
                    click('Share web tabs')
                    send('tabs')
                    shown('Browser actions dispatched')
                    click('Include page text')
                    send('page')
                    shown('Fixture page received')
                    report['checks'].append('explicit tab metadata and page-text sharing, direct tab activation')
                    assert 'fixture-password-do-not-share' not in json.dumps(ModelFixture.requests[-1][1])
                    click('Include page text')
                    send('stale')
                    press(ord('l'), control=True)
                    driver.replace(base+'/moved')
                    press(0xff0d)
                    wait('manual page change', lambda _: any('/moved' in x['url'] for x in pages()))
                    before = len(pages())
                    shown('A target tab changed or closed')
                    assert len(pages()) == before
                    send('close')
                    shown('Browser actions dispatched')
                    wait('approved close', lambda _: len(pages()) == before-1)
                    report['checks'].append('password field excluded; stale targets blocked; direct tab close')
                    click('Include page text')
                    click('Include page text')
                    click('Share web tabs')
                    send('settings')
                    shown('Browser actions dispatched')
                    report['checks'].append('direct browser theme and vertical-tab settings dispatch')
                    send('invalid')
                    shown('The model returned an unsupported response')
                    assert not any(n.get_name() == 'Run these actions' for n in nodes())
                    report['checks'].append('unsafe model actions rejected atomically')
                    send('slow')
                    shown('Thinking…')
                    time.sleep(.4)
                    subprocess.run(['import', '-window', hex(driver.window), str(output/'processing.png')], check=True, timeout=5, capture_output=True)
                    click('Stop')
                    time.sleep(4.3)
                    assert not any('Cancelled response must not appear' in n.get_name() for n in nodes())
                    report['checks'].append('Stop cancels and ignores late model responses')
                    open_settings()
                    click('Use an API key provider')
                    find('Custom model name')
                    text('Endpoint URL', base+'/v1/chat/completions')
                    text('Custom model name', 'fixture-api')
                    text('API key (session only)', 'fixture-secret-never-persist')
                    click('Save settings')
                    send('hello')
                    wait('API request', lambda _: ModelFixture.requests[-1][0] == '/v1/chat/completions')
                    shown('Fixture reply')
                    assert ModelFixture.requests[-1][2] == 'Bearer fixture-secret-never-persist'
                    report['checks'].append('OpenAI-compatible API request and bearer credential')
                    open_settings()
                    text('Endpoint URL', base+'/redirect')
                    click('Save settings')
                    send('hello')
                    shown('Redirect blocked')
                    assert not any(x[0] == '/credential-sink' for x in ModelFixture.requests)
                    report['checks'].append('redirect blocked without forwarding API credential')
                    # Capture the real native panel, without displaying fixture credentials.
                    subprocess.run(['import', '-window', hex(driver.window), str(output/'panel.png')], check=True, timeout=5, capture_output=True)
                    press(ord('a'), control=True, alt=True)
                    wait('closed panel', lambda ns: not panel(ns))
                    press(ord('a'), control=True, alt=True)
                    click('Settings')
                    assert Atspi.Text.get_text(find('API key (session only)'), 0, -1) == ''
                    key_field = find('API key (session only)')
                    if not Atspi.Component.grab_focus(key_field):
                        bounds = Atspi.Component.get_extents(key_field, Atspi.CoordType.WINDOW)
                        driver.click(bounds.x + bounds.width // 2, bounds.y + bounds.height // 2)
                    press(ord('z'), control=True)
                    assert Atspi.Text.get_text(find('API key (session only)'), 0, -1) == '' 
                    report['checks'].append('shortcut toggles; hidden panel cancels and clears session key')
                    click('New chat')
                    assert not any('Fixture reply' in n.get_name() for n in nodes())
                    open_settings()
                    click('Turn on Blynx assistant')
                    count = len(ModelFixture.requests)
                    send('hello')
                    time.sleep(.4)
                    assert len(ModelFixture.requests) == count
                    report['checks'].append('new chat clears history; disabling prevents network requests')
                    browser.send_signal(signal.SIGINT)
                    assert browser.wait(timeout=15) == 0
                    saved = (default/'Preferences').read_text()
                    assert 'fixture-secret-never-persist' not in saved
                    data = json.loads(saved)['blynx']['assistant_config']
                    assert data['provider'] == 'api' and data['model'] == 'fixture-api' and not data['enabled']
                    report['checks'].append('only non-secret connection configuration persists on clean exit')
                    browser = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                    wait('restarted browser', lambda ns: any(n.get_name() == 'Address and search bar' for n in ns))
                    driver = NativeX11Input(browser.pid, 'Assistant fixture')
                    press(ord('a'), control=True, alt=True)
                    open_settings()
                    assert Atspi.Text.get_text(find('Custom model name'), 0, -1) == 'fixture-api'
                    assert Atspi.Text.get_text(find('API key (session only)'), 0, -1) == ''
                    assert not find('Turn on Blynx assistant').get_state_set().contains(Atspi.StateType.CHECKED)
                    assert len(ModelFixture.requests) == count
                    browser.send_signal(signal.SIGINT)
                    assert browser.wait(timeout=15) == 0
                    report['checks'].append('restart restores configuration with no key and no automatic request')
                    browser = subprocess.Popen(command[:-2]+['--incognito']+command[-2:], env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                    wait('private browser', lambda ns: any(n.get_name() == 'Address and search bar' for n in ns))
                    driver = NativeX11Input(browser.pid, 'Assistant fixture')
                    press(ord('a'), control=True, alt=True)
                    time.sleep(.5)
                    assert not panel(nodes()) and len(ModelFixture.requests) == count
                    browser.send_signal(signal.SIGINT)
                    assert browser.wait(timeout=15) == 0
                    report['checks'].append('private window cannot open assistant or make model requests')
                    report['passed'] = True
                finally:
                    if browser.poll() is None:
                        browser.send_signal(signal.SIGINT)
                        try: browser.wait(timeout=15)
                        except subprocess.TimeoutExpired: browser.kill(); browser.wait()
    except Exception as error:
        report['error'] = str(error)
        report['fixture_prompts'] = [json.loads(r[1]['messages'][-1]['content'])['request'] for r in ModelFixture.requests]
        raise
    finally:
        server.shutdown()
        report_path.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
