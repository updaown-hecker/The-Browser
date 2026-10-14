// Run by scripts/runtime_smoke.py against an isolated local browser profile.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

const [port, fixture, output] = process.argv.slice(2);
const report = {checks: [], sandbox: '', pageErrors: []};

async function connect(url) {
  const socket = new WebSocket(url);
  await new Promise((resolve, reject) => {
    socket.addEventListener('open', resolve, {once: true});
    socket.addEventListener('error', reject, {once: true});
  });
  let sequence = 0;
  const pending = new Map();
  socket.addEventListener('message', event => {
    const message = JSON.parse(event.data);
    if (message.method === 'Runtime.exceptionThrown') {
      report.pageErrors.push(message.params.exceptionDetails.text);
    }
    const waiter = pending.get(message.id);
    if (waiter) {
      pending.delete(message.id);
      clearTimeout(waiter.timer);
      message.error ? waiter.reject(new Error(JSON.stringify(message.error))) :
                      waiter.resolve(message.result);
    }
  });
  return {
    close: () => socket.close(),
    send(method, params = {}) {
      return new Promise((resolve, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => {
          pending.delete(id);
          reject(new Error(`Timed out: ${method}`));
        }, 15000);
        pending.set(id, {resolve, reject, timer});
        socket.send(JSON.stringify({id, method, params}));
      });
    },
  };
}

async function evaluate(client, expression) {
  const response = await client.send('Runtime.evaluate', {
    expression, returnByValue: true, awaitPromise: true,
  });
  assert(!response.exceptionDetails, JSON.stringify(response.exceptionDetails));
  return response.result.value;
}

async function waitFor(client, expression) {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    if (await evaluate(client, expression)) return;
    await new Promise(resolve => setTimeout(resolve, 200));
  }
  throw new Error(`Page condition timed out: ${expression}`);
}

const metadata = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
const browser = await connect(metadata.webSocketDebuggerUrl);
const sockets = [browser];
try {
  const {targetId} = await browser.send('Target.createTarget', {url: 'chrome://newtab/'});
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const page = await connect(targets.find(target => target.id === targetId).webSocketDebuggerUrl);
  sockets.push(page);
  await page.send('Runtime.enable');
  await page.send('Page.enable');
  await page.send('Emulation.setDeviceMetricsOverride', {
    width: 1280, height: 800, deviceScaleFactor: 1, mobile: false,
  });
  await waitFor(page, `document.querySelector('ntp-app')?.shadowRoot?.querySelector('#browserFinalBrand')?.textContent.includes('Blynx')`);
  const links = await evaluate(page, `Array.from(document.querySelector('ntp-app').shadowRoot.querySelectorAll('#browserFinalNavigation a'), a => ({text: a.textContent.trim(), href: a.href}))`);
  assert.equal(links.length, 6);
  assert(links.every(link => !link.text.includes('&')), 'Menu accelerator characters leaked into new-tab labels');
  assert.deepEqual(links.map(link => link.href), [
    'blynx://bookmarks/', 'blynx://history/', 'blynx://downloads/',
    'blynx://extensions/', 'blynx://settings/', 'blynx://webstore/',
  ]);
  report.checks.push('branded new-tab header and six browser shortcuts');
  await waitFor(page, `!!document.querySelector('ntp-app').shadowRoot.querySelector('ntp-searchbox')`);
  report.checks.push('upstream new-tab search component present');
  for (let index = 0; index < 3; ++index) {
    const extra = await browser.send('Target.createTarget', {url: 'chrome://newtab/'});
    const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    const extraPage = await connect(list.find(target => target.id === extra.targetId).webSocketDebuggerUrl);
    sockets.push(extraPage);
    await waitFor(extraPage, `document.querySelector('ntp-app')?.shadowRoot?.querySelector('#browserFinalBrand')?.textContent.includes('Blynx')`);
    await browser.send('Target.closeTarget', {targetId: extra.targetId});
  }
  report.checks.push('three additional new tabs render and close without a browser crash');
  const screenshot = await page.send('Page.captureScreenshot');
  await fs.writeFile(`${output}/new-tab.png`, Buffer.from(screenshot.data, 'base64'));
  await evaluate(page, `document.querySelector('ntp-app').shadowRoot.querySelector('#browserFinalNavigation a').click()`);
  await waitFor(page, `location.href.startsWith('chrome://bookmarks') && !!document.querySelector('bookmarks-app')`);
  report.checks.push('bookmark shortcut navigates to native bookmark WebUI');


  // Exercise the real Settings page controls, backed by the guarded handler.
  await page.send('Page.navigate', {url: 'blynx://settings/appearance'});
  const deepFind = `(selector, root = document) => {
    const found = root.querySelector(selector);
    if (found) return found;
    for (const node of root.querySelectorAll('*')) {
      if (node.shadowRoot) {
        const match = window.blynxFind(selector, node.shadowRoot);
        if (match) return match;
      }
    }
    return null;
  }`;
  await waitFor(page, `location.href === 'chrome://settings/appearance' && !!document.querySelector('settings-ui')`);
  await evaluate(page, `window.blynxFind = ${deepFind}`);
  await waitFor(page, `!!window.blynxFind('#blynxAccent')`);
  report.checks.push('blynx://settings/appearance resolves to the original trusted Chromium Settings origin');
  const getState = `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('getBlynxAppearance'))`;
  const before = await evaluate(page, getState);
  assert(before.editable);
  await evaluate(page, `(() => {
    const input = window.blynxFind('#blynxAccent');
    input.value = '#3D8D88'; input.dispatchEvent(new Event('input', {bubbles:true}));
    const style = window.blynxFind('#blynxStyle');
    style.value = 'midnight'; style.dispatchEvent(new Event('change', {bubbles:true}));
    window.blynxFind('#blynxVertical').click();
    window.blynxFind('#blynxApply').click();
  })()`);
  await waitFor(page, `window.blynxFind('#blynxStatus').textContent.includes('Saved.')`);
  const saved = await evaluate(page, getState);
  assert.equal(JSON.parse(saved.preset).accent_color, '#3D8D88');
  assert.equal(saved.style, 'midnight');
  assert.equal(saved.vertical, true);
  report.checks.push('Settings Apply saves the selected accent, vertical tabs and new-tab canvas');
  const unsupportedStyle = await evaluate(page, `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('applyBlynxAppearance', ${JSON.stringify(saved.preset)}, true, 'unknown-style', true))`);
  assert.equal(unsupportedStyle.saved, false);
  assert.equal(unsupportedStyle.preset, saved.preset);
  assert.equal(unsupportedStyle.style, 'midnight');
  report.checks.push('unknown new-tab canvas is refused without partially applying appearance');
  const settingsScreenshot = await page.send('Page.captureScreenshot');
  await fs.writeFile(`${output}/settings.png`, Buffer.from(settingsScreenshot.data, 'base64'));
  await evaluate(page, `(() => {
    const input = window.blynxFind('#blynxAccent');
    input.value = 'INVALID'; input.dispatchEvent(new Event('input', {bubbles:true}));
    window.blynxFind('#blynxApply').click();
  })()`);
  await waitFor(page, `window.blynxFind('#blynxStatus').textContent.includes('could not be saved')`);
  assert.equal((await evaluate(page, getState)).preset, saved.preset);
  report.checks.push('invalid Settings accent is refused without replacing saved preferences');
  await evaluate(page, `window.blynxFind('settings-appearance-page').onBlynxCancelClick_()`);
  assert.equal(await evaluate(page, `window.blynxFind('#blynxAccent').value`), '#3D8D88');
  report.checks.push('Settings Cancel restores the saved configuration');
  await page.send('Page.navigate', {url:'chrome://settings/appearance'});
  await waitFor(page, `location.href === 'chrome://settings/appearance' && !!document.querySelector('settings-ui')`);
  await evaluate(page, `window.blynxFind = ${deepFind}`);
  await waitFor(page, `window.blynxFind('#blynxAccent')?.value === '#3D8D88'`);
  report.checks.push('legacy chrome:// Settings links still work and reload saved appearance');
  await page.send('Page.navigate', {url:'blynx://newtab/'});
  await waitFor(page, `document.querySelector('ntp-app')?.shadowRoot?.querySelector('#content.midnight')`);
  report.checks.push('saved new-tab canvas renders through blynx://newtab');
  await evaluate(page, `document.querySelector('ntp-app').onCustomizeClick_()`);
  await waitFor(page, `location.href === 'chrome://settings/appearance'`);
  report.checks.push('new-tab Customize opens Settings instead of a popup');

  await page.send('Page.navigate', {url: fixture});
  await waitFor(page, `document.title === 'Blynx local smoke'`);
  assert.equal(await evaluate(page, 'document.querySelector("#result").textContent'), 'JavaScript executed');
  assert.equal(await evaluate(page, 'typeof chrome?.send'), 'undefined');
  report.checks.push('ordinary local website has no trusted Settings message API');
  await evaluate(page, 'document.querySelector("#next").click()');
  await waitFor(page, `document.title === 'Second local page'`);
  report.checks.push('local HTTP page, JavaScript, and link navigation');
  const second = await browser.send('Target.createTarget', {url: fixture});
  await browser.send('Target.activateTarget', {targetId: second.targetId});
  await browser.send('Target.closeTarget', {targetId: second.targetId});
  report.checks.push('create, activate, and close a second tab');

  await browser.send('Browser.setDownloadBehavior', {
    behavior: 'allow', downloadPath: `${output}/downloads`, eventsEnabled: true,
  });
  await fs.rm(`${output}/downloads/smoke.txt`, {force: true});
  await page.send('Page.navigate', {url: fixture});
  await waitFor(page, `document.title === 'Blynx local smoke'`);
  await evaluate(page, 'document.querySelector("#download").click()');
  const deadline = Date.now() + 15000;
  let downloaded = false;
  while (Date.now() < deadline) {
    try {
      downloaded = (await fs.readFile(`${output}/downloads/smoke.txt`, 'utf8')) === 'Blynx download passed\n';
      if (downloaded) break;
    } catch {}
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  assert(downloaded, 'Local download did not complete with the expected content');
  report.checks.push('local file download and content verification');

  await page.send('Page.navigate', {url: 'chrome://extensions/'});
  await waitFor(page, `!!globalThis.chrome?.developerPrivate?.getExtensionsInfo`);
  const extensions = await evaluate(page, `chrome.developerPrivate.getExtensionsInfo({includeDisabled: true, includeTerminated: true})`);
  const extension = extensions.find(item => item.name === 'Blynx local smoke extension');
  assert(extension, 'Local unpacked MV3 extension was not loaded');
  assert.equal(extension.state, 'ENABLED');
  await page.send('Page.navigate', {url: `chrome-extension://${extension.id}/check.html`});
  await waitFor(page, `document.title === 'Blynx extension smoke'`);
  const firstReply = await evaluate(page, `chrome.runtime.sendMessage({operation: 'roundtrip', value: 42})`);
  assert.equal(firstReply.value, 42);
  report.checks.push('unpacked MV3 worker messages and storage round-trip');
  await page.send('Page.navigate', {url: 'chrome://extensions/'});
  await waitFor(page, `!!globalThis.chrome?.management?.setEnabled`);
  await evaluate(page, `chrome.management.setEnabled('${extension.id}', false)`);
  let state = await evaluate(page, `chrome.developerPrivate.getExtensionInfo('${extension.id}')`);
  assert.equal(state.state, 'DISABLED');
  await evaluate(page, `chrome.management.setEnabled('${extension.id}', true)`);
  await page.send('Page.navigate', {url: `chrome-extension://${extension.id}/check.html`});
  await waitFor(page, `document.title === 'Blynx extension smoke'`);
  const stored = await evaluate(page, `chrome.storage.local.get('smokeValue')`);
  assert.equal(stored.smokeValue, 42);
  const secondReply = await evaluate(page, `chrome.runtime.sendMessage({operation: 'roundtrip', value: 84})`);
  assert.equal(secondReply.value, 84);
  report.checks.push('extension disable/re-enable preserves storage and restarts worker');

  await page.send('Page.navigate', {url: 'chrome://sandbox/'});
  await waitFor(page, `document.body?.innerText.includes('Seccomp')`);
  report.sandbox = await evaluate(page, 'document.body.innerText');
  assert.match(report.sandbox, /Seccomp-BPF sandbox\s+Yes/);
  report.checks.push('chrome://sandbox reports Seccomp-BPF sandbox enabled');
  report.passed = true;
} catch (error) {
  report.passed = false;
  report.error = String(error.stack || error);
  process.exitCode = 1;
} finally {
  await fs.writeFile(`${output}/results.json`, JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report, null, 2));
  for (const socket of sockets) socket.close();
}
