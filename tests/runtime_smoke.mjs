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
  await waitFor(page, `document.querySelector('ntp-app')?.shadowRoot?.querySelector('#browserFinalBrand')?.textContent.includes('Browser Final')`);
  const links = await evaluate(page, `Array.from(document.querySelector('ntp-app').shadowRoot.querySelectorAll('#browserFinalNavigation a'), a => ({text: a.textContent.trim(), href: a.href}))`);
  assert.equal(links.length, 5);
  assert.deepEqual(links.map(link => link.href), [
    'chrome://bookmarks/', 'chrome://history/', 'chrome://downloads/',
    'chrome://extensions/', 'chrome://settings/',
  ]);
  report.checks.push('branded new-tab header and five browser shortcuts');
  await waitFor(page, `!!document.querySelector('ntp-app').shadowRoot.querySelector('ntp-searchbox')`);
  report.checks.push('upstream new-tab search component present');
  const screenshot = await page.send('Page.captureScreenshot');
  await fs.writeFile(`${output}/new-tab.png`, Buffer.from(screenshot.data, 'base64'));
  await evaluate(page, `document.querySelector('ntp-app').shadowRoot.querySelector('#browserFinalNavigation a').click()`);
  await waitFor(page, `location.href.startsWith('chrome://bookmarks') && !!document.querySelector('bookmarks-app')`);
  report.checks.push('bookmark shortcut navigates to native bookmark WebUI');

  await page.send('Page.navigate', {url: fixture});
  await waitFor(page, `document.title === 'Browser Final local smoke'`);
  assert.equal(await evaluate(page, 'document.querySelector("#result").textContent'), 'JavaScript executed');
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
  await waitFor(page, `document.title === 'Browser Final local smoke'`);
  await evaluate(page, 'document.querySelector("#download").click()');
  const deadline = Date.now() + 15000;
  let downloaded = false;
  while (Date.now() < deadline) {
    try {
      downloaded = (await fs.readFile(`${output}/downloads/smoke.txt`, 'utf8')) === 'Browser Final download passed\n';
      if (downloaded) break;
    } catch {}
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  assert(downloaded, 'Local download did not complete with the expected content');
  report.checks.push('local file download and content verification');

  await page.send('Page.navigate', {url: 'chrome://extensions/'});
  await waitFor(page, `!!globalThis.chrome?.developerPrivate?.getExtensionsInfo`);
  const extensions = await evaluate(page, `chrome.developerPrivate.getExtensionsInfo({includeDisabled: true, includeTerminated: true})`);
  const extension = extensions.find(item => item.name === 'Browser Final local smoke extension');
  assert(extension, 'Local unpacked MV3 extension was not loaded');
  assert.equal(extension.state, 'ENABLED');
  await page.send('Page.navigate', {url: `chrome-extension://${extension.id}/check.html`});
  await waitFor(page, `document.title === 'Browser Final extension smoke'`);
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
  await waitFor(page, `document.title === 'Browser Final extension smoke'`);
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
