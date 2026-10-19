#!/usr/bin/env python3
"""Check compiled branding using an isolated browser and intercepted Store fixtures.

A real HTTPS Store robots.txt response supplies upstream security headers.
DevTools then replaces its body with controlled test buttons; other origins use
intercepted fixtures.
No extension is installed, no verifier is changed, and no personal profile is used.
"""
import json
import gzip
import os
import re
import sys
from pathlib import Path
import signal
import subprocess
import tempfile
import time

from bootstrap import resolve_paths
from build import _require_integration, _require_completed_browser_build
from launch import installed_sandbox_helper


def main(script_name='branding_smoke.mjs', output_name='branding-smoke'):
    root = Path(__file__).resolve().parents[1]
    source = root / '.local/chromium/src'
    output = root / '.local' / output_name
    output.mkdir(exist_ok=True)
    (output/'results.json').write_text(json.dumps({'passed': False, 'checks': []}))
    paths = resolve_paths(root, source_root=str(source),
                          build_root=str(source/'out/linux-release'), platform_name='linux')
    _require_integration(paths)
    _require_completed_browser_build(paths)
    replacements = json.loads((root/'browser/resources/icons/chromium-replacements.json').read_text())
    for target, asset in replacements.items():
        assert (source/target).read_bytes() == (root/'browser/resources'/asset).read_bytes(), target
    version_assets = json.loads((root/'browser/resources/icons/version-replacements.json').read_text())
    for target, asset in version_assets.items():
        assert (source/target).read_bytes() == (root/'browser/resources'/asset).read_bytes(), target
    sys.path.insert(0, str(source/'tools/grit'))
    from grit.format.data_pack import ReadDataPack
    headers = (paths.build_root/'gen/chrome/grit/chrome_unscaled_resources.h').read_text()
    packed = ReadDataPack(str(paths.build_root/'resources.pak')).resources
    for size in (64, 128, 256):
        resource = int(re.search(rf'#define IDR_PRODUCT_LOGO_{size} (\d+)', headers).group(1))
        assert packed[resource] == (root/f'browser/resources/icons/blynx-product-{size}.png').read_bytes()
    resource = int(re.search(r'#define IDR_PRODUCT_LOGO_SVG (\d+)', headers).group(1))
    svg = packed[resource]
    if svg.startswith(b'\x1f\x8b'):
        svg = gzip.decompress(svg)
    assert svg == (root/'browser/resources/product-icon.svg').read_bytes()
    webui = (paths.build_root/'gen/ui/webui/resources/grit/webui_resources.h').read_text()
    dark_id = int(re.search(r'#define IDR_WEBUI_IMAGES_CHROME_LOGO_DARK_SVG (\d+)', webui).group(1))
    dark = packed[dark_id]
    if dark.startswith(b'\x1f\x8b'):
        dark = gzip.decompress(dark)
    assert dark == (root/'browser/resources/blynx-eye-light.svg').read_bytes()
    scaled_header = (paths.build_root/'gen/components/grit/components_scaled_resources.h').read_text()
    for scale in (100, 200):
        scaled = ReadDataPack(str(paths.build_root/f'chrome_{scale}_percent.pak')).resources
        for name, suffix in [('IDR_PRODUCT_LOGO', ''), ('IDR_PRODUCT_LOGO_WHITE', '_white')]:
            rid = int(re.search(rf'#define {name} (\d+)', scaled_header).group(1))
            assert scaled[rid] == (root/f'browser/resources/icons/blynx-version-wordmark-{scale//100}x{suffix}.png').read_bytes()
    strings = (paths.build_root/'gen/extensions/strings/grit/extensions_strings.h').read_text()
    accept = int(re.search(r'#define IDS_EXTENSION_INSTALL_PROMPT_ACCEPT_BUTTON_EXTENSION (\d+)', strings).group(1))
    locale = ReadDataPack(str(paths.build_root/'locales/en-US.pak'))
    assert locale.resources[accept].decode('utf-8' if locale.encoding == 1 else 'utf-16-le') == 'Add to Blynx'
    env = os.environ.copy()
    helper = Path('/opt/google/chrome/chrome-sandbox')
    if installed_sandbox_helper(helper):
        env.setdefault('CHROME_DEVEL_SANDBOX', str(helper))
    with tempfile.TemporaryDirectory(prefix='branding-profile-', dir=root/'.local') as profile:
        default = Path(profile)/'Default'
        default.mkdir()
        (default/'Preferences').write_text(json.dumps({'blynx': {'onboarding_step': 4}}))
        with (output/'browser.log').open('w') as log:
            fixture_args = ['--host-resolver-rules=MAP search.brave.com ~NOTFOUND'] if script_name == 'newtab_smoke.mjs' else []
            browser = subprocess.Popen([str(paths.build_root/'chrome'), *fixture_args, '--user-data-dir='+profile,
                '--headless', '--force-dark-mode', '--no-first-run', '--disable-background-networking',
                '--remote-debugging-address=127.0.0.1', '--remote-debugging-port=0', 'about:blank'],
                env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                active = Path(profile)/'DevToolsActivePort'
                deadline = time.monotonic()+30
                while not active.exists():
                    if browser.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('Fixture browser did not start')
                    time.sleep(.1)
                port = active.read_text().splitlines()[0]
                subprocess.run([str(source/'third_party/node/linux/node-linux-x64/bin/node'),
                    str(root/'tests'/script_name), port, str(output)], check=True, timeout=120)
                browser.send_signal(signal.SIGINT)
                assert browser.wait(timeout=15) == 0
            finally:
                if browser.poll() is None:
                    browser.send_signal(signal.SIGINT)
                    try:
                        browser.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        browser.kill()
                        browser.wait()
    report = json.loads((output/'results.json').read_text())
    report['checks'].append(f'all {len(replacements)+len(version_assets)} mapped source assets and version wordmark packs match; compiled 64/128/256/SVG/dark-mode resources and native Add to Blynx string match owned branding')
    (output/'results.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Verified all', len(replacements), 'product-resource replacements and compiled branding')


if __name__ == '__main__':
    main()
