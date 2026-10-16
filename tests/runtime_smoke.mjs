// Run by scripts/runtime_smoke.py against an isolated local browser profile.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

const [port, fixture, output, phase] = process.argv.slice(2);
const report = phase !== 'initial' ? JSON.parse(await fs.readFile(`${output}/results.json`, 'utf8')) :
    {checks: [], sandbox: '', pageErrors: []};

async function connect(url) {
  const socket = new WebSocket(url);
  await new Promise((resolve, reject) => {
    socket.addEventListener('open', resolve, {once: true});
    socket.addEventListener('error', reject, {once: true});
  });
  let sequence = 0;
  const pending = new Map();
  const listeners = new Map();
  socket.addEventListener('message', event => {
    const message = JSON.parse(event.data);
    listeners.get(message.method)?.(message.params);
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
  socket.addEventListener('close', () => {
    for (const waiter of pending.values()) {
      clearTimeout(waiter.timer);
      waiter.reject(new Error('DevTools disconnected'));
    }
    pending.clear();
  });
  return {
    close: () => socket.close(),
    on: (method, listener) => listeners.set(method, listener),
    send(method, params = {}) {
      if (socket.readyState !== WebSocket.OPEN) return Promise.reject(new Error('DevTools disconnected'));
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

const metadata = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
const browser = await connect(metadata.webSocketDebuggerUrl);
const sockets = [browser];
async function assertNativeSetupTarget(label) {
  if (!output.includes('native-smoke')) return;
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    if (list.some(target => target.url.replace(/^blynx:/, 'chrome:') === 'chrome://settings/appearance')) {
      report.checks.push(label);
      return;
    }
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  throw new Error('Native startup did not automatically present setup');
}
try {
  if (phase === 'completed') {
    // Allow the native first-window hook to run before checking suppression.
    await new Promise(resolve => setTimeout(resolve, 1500));
    const startupTargets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    report.restoredSettingsTabs = startupTargets.filter(target => target.url.replace(/^blynx:/, 'chrome:') === 'chrome://settings/appearance').length;
    const {targetId} = await browser.send('Target.createTarget', {url: 'chrome://settings/appearance'});
    const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    const page = await connect(targets.find(target => target.id === targetId).webSocketDebuggerUrl);
    sockets.push(page);
    await waitFor(page, `!!document.querySelector('settings-ui')`);
    const setup = await evaluate(page, `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('blynxOnboarding', 'get'))`);
    assert.equal(setup.step, 4);
    await evaluate(page, `window.blynxFind = ${deepFind}`);
    await waitFor(page, `!!window.blynxFind('#blynxSetupStart')`);
    assert.equal(await evaluate(page, `!!window.blynxFind('#blynxSetupHeading')`), false);
    report.checks.push('completed onboarding survives another restart and its wizard stays hidden');
    report.passed = true;
    delete report.error;
    await fs.writeFile(`${output}/results.json`, JSON.stringify(report, null, 2));
    console.log(JSON.stringify({passed: true, checks: report.checks.length, phase}));
    process.exitCode = 0;
  } else if (phase === 'restart') {
    await assertNativeSetupTarget('native restart automatically resumes unfinished onboarding');
    const {targetId} = await browser.send('Target.createTarget', {url: 'chrome://settings/appearance'});
    const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    const page = await connect(targets.find(target => target.id === targetId).webSocketDebuggerUrl);
    sockets.push(page);
    await page.send('Runtime.enable');
    await page.send('Page.enable');
    await waitFor(page, `!!document.querySelector('settings-ui')`);
    const setup = await evaluate(page, `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('blynxOnboarding', 'get'))`);
    assert.equal(setup.step, 3);
    assert.equal(setup.editable, true);
    report.checks.push('unfinished onboarding progress survives a clean browser restart');
    const completedSetup = await evaluate(page, `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('blynxOnboarding', 'finish'))`);
    assert.equal(completedSetup.step, 4);
    assert.equal(completedSetup.saved, true);
    assert.equal((await evaluate(page, `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('blynxOnboarding', 'next'))`)).saved, false);
    report.checks.push('finishing setup persists completion and refuses further next transitions');
    const restored = await evaluate(page, `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('getBlynxAppearance'))`);
    assert.deepEqual(restored, report.expectedSavedAppearance);
    report.checks.push('saved appearance, vertical tabs and canvas preferences survive a clean browser restart');
    const searchRestored=await evaluate(page, `import('chrome://resources/js/cr.js').then(m=>m.sendWithPromise('getBlynxSearchAppearance'))`);
    assert.deepEqual(searchRestored.config, report.expectedSearchAppearance);
    await evaluate(page, `window.blynxFind = ${deepFind}`);
    await waitFor(page, `!!window.blynxFind('#search-outer_style')`);
    assert.deepEqual(await evaluate(page, `['outer_style','hover_style','input_style','palette'].map(key=>window.blynxFind('#search-'+key).value)`),
        ['md','sm','pulse-inner','custom']);

    report.checks.push('custom search beam and interface colors survive clean browser restart');

    await page.send('Page.navigate', {url:'blynx://newtab/'});
    await waitFor(page, `document.querySelector('ntp-app')?.shadowRoot?.querySelector('#content.midnight')`);
    report.checks.push('saved new-tab canvas renders in the restarted browser');
    await page.send('Page.navigate', {url: fixture});
    await waitFor(page, `document.title === 'Blynx local smoke'`);
    assert.equal(await evaluate(page, `localStorage.getItem('blynxIsolation')`), 'regular');
    report.checks.push('regular site storage survives a clean browser restart');
    await page.send('Page.navigate', {url: 'blynx://bookmarks/'});
    await waitFor(page, `!!globalThis.chrome?.bookmarks?.get`);
    const restoredBookmark = await evaluate(page, `chrome.bookmarks.get(${JSON.stringify(report.expectedBookmark.id)})`);
    assert.equal(restoredBookmark[0].title, report.expectedBookmark.title);
    assert.equal(restoredBookmark[0].url, fixture);
    await evaluate(page, `chrome.bookmarks.remove(${JSON.stringify(report.expectedBookmark.id)})`);
    report.checks.push('bookmark survives a clean browser restart and remains editable');
    await page.send('Page.navigate', {url:'chrome://extensions/'});
    await waitFor(page, `!!globalThis.chrome?.developerPrivate?.getExtensionsInfo`);
    const extensions = await evaluate(page, `chrome.developerPrivate.getExtensionsInfo({includeDisabled:true})`);
    const extension = extensions.find(item => item.name === 'Blynx local smoke extension');
    assert(extension);
    await page.send('Page.navigate', {url:`chrome-extension://${extension.id}/check.html`});
    await waitFor(page, `document.title === 'Blynx extension smoke'`);
    assert.equal((await evaluate(page, `chrome.storage.local.get('smokeValue')`)).smokeValue, 84);
    report.checks.push('MV3 extension local storage survives a clean browser restart');
  } else {
  await assertNativeSetupTarget('new native profile automatically presents first-run onboarding');
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

  await waitFor(page, `location.href === 'chrome://settings/appearance' && !!document.querySelector('settings-ui')`);
  await evaluate(page, `window.blynxFind = ${deepFind}`);
  await waitFor(page, `!!window.blynxFind('#blynxAccent')`);
  report.checks.push('blynx://settings/appearance resolves to the original trusted Chromium Settings origin');
  const getState = `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('getBlynxAppearance'))`;
  const getSearch = `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('getBlynxSearchAppearance'))`;
  await waitFor(page, `!!window.blynxFind('#search-outer_style')`);
  const searchInitial = await evaluate(page, getSearch);
  assert.equal(searchInitial.editable, true);
  assert.deepEqual(await evaluate(page, `['outer_style','hover_style','input_style'].map(key=>window.blynxFind('#search-'+key).value)`),
      ['pulse-outside','pulse-inner','line']);

  assert.deepEqual(await evaluate(page, `Array.from(window.blynxFind('#search-outer_style').options, o => o.value)`), ['md','sm','line','pulse-inner','pulse-outside']);
  assert.deepEqual(await evaluate(page, `Array.from(window.blynxFind('#search-palette').options, o => o.value)`), ['colorful','mono','ocean','sunset','custom']);
  report.checks.push('search settings expose all five beam styles, four palettes plus custom, theme, strength and motion');
  await evaluate(page, `(() => {
    const field = window.blynxFind('#search-outer_style'); field.value='sm'; field.dispatchEvent(new Event('change', {bubbles:true}));
  })()`);
  assert.deepEqual((await evaluate(page,getSearch)).config,searchInitial.config);
  await evaluate(page, `window.blynxFind('#search-cancel').click()`);
  await waitFor(page, `window.blynxFind('#search-outer_style').value === 'pulse-outside'`);
  report.checks.push('search appearance edits are staged and Cancel restores saved settings without writing');
  await evaluate(page, `(() => {
    const change=(selector,value,event='change')=>{const el=window.blynxFind(selector); if(el.type==='checkbox')el.checked=value;else el.value=String(value);el.dispatchEvent(new Event(event,{bubbles:true}));};
    change('#search-custom-colors',true);change('#search-active',false);
    change('#search-outer_style','md');change('#search-hover_style','sm');change('#search-input_style','pulse-inner');
    change('#search-theme','dark');change('#search-strength',.45,'input');change('#search-duration',14,'input');
    window.blynxFind('#blynxSearchAppearance').querySelectorAll('details').forEach(d=>d.open=true);
  })()`);
  await waitFor(page, `!window.blynxFind('#search-color-surface').matches(':disabled')`);
  await evaluate(page, `(() => {
    for(const [selector,value] of [['#search-color-surface','#102030'],['#search-color-input_text','#F0E0D0'],['#search-beam-1','#FF44AA']]) {
      const el=window.blynxFind(selector);el.value=value;el.dispatchEvent(new Event('input',{bubbles:true}));
    }
  })()`);
  await waitFor(page, `window.blynxFind('#search-palette').value === 'custom'`);
  assert.equal(await evaluate(page, `getComputedStyle(window.blynxFind('.blynx-search-preview')).backgroundColor`),'rgb(16, 32, 48)');
  await evaluate(page, `window.blynxFind('#search-apply').click()`);
  await waitFor(page, `!window.blynxFind('settings-appearance-page').blynxSearchBusy_ && window.blynxFind('#search-status').textContent.trim() !== ''`);
  const searchSaved = await evaluate(page,getSearch);
  assert.equal(searchSaved.config.colors.surface,'#102030');assert.equal(searchSaved.config.colors.input_text,'#F0E0D0');
  assert.equal(searchSaved.config.palette,'custom');assert.equal(searchSaved.config.strength,.45);assert.equal(searchSaved.config.duration,14);
  assert.equal(searchSaved.config.active,false);assert.equal(searchSaved.config.custom_colors,true);
  report.expectedSearchAppearance=searchSaved.config;
  report.checks.push('search appearance Apply saves component styles, custom colors, strength, speed, theme and paused animation');
  for (const invalid of [
    {...searchSaved.config,strength:1.1}, {...searchSaved.config,input_style:'invalid'},
    {...searchSaved.config,colors:{...searchSaved.config.colors,surface:'url(x)'}}, {...searchSaved.config,extra:true},
  ]) {
    const result=await evaluate(page, `import('chrome://resources/js/cr.js').then(m=>m.sendWithPromise('applyBlynxSearchAppearance',${JSON.stringify(invalid)}))`);
    assert.equal(result.saved,false);assert.deepEqual(result.config,searchSaved.config);
  }
  report.checks.push('native search settings reject out-of-range strength, unknown styles/fields and non-RGB colors atomically');
  await evaluate(page, `window.blynxFind('#search-reset').click()`);
  await waitFor(page, `window.blynxFind('#search-palette').value === 'colorful'`);
  assert.deepEqual((await evaluate(page,getSearch)).config,searchSaved.config);
  await evaluate(page, `window.blynxFind('#search-cancel').click()`);
  await waitFor(page, `window.blynxFind('#search-palette').value === 'custom'`);
  report.checks.push('Reset to defaults is staged and Cancel restores the saved custom search design');
  await evaluate(page, `window.blynxFind('#blynxSearchHeading').scrollIntoView({block:'start'})`);
  await fs.writeFile(`${output}/search-appearance.png`,Buffer.from((await page.send('Page.captureScreenshot')).data,'base64'));
  await evaluate(page, `window.blynxFind('#search-color-surface').scrollIntoView({block:'center'})`);
  await fs.writeFile(`${output}/search-colors.png`,Buffer.from((await page.send('Page.captureScreenshot')).data,'base64'));

  const identityLabels = await evaluate(page, `import('chrome://resources/js/load_time_data.js').then(m => [m.loadTimeData.getString('peoplePageTitle'), m.loadTimeData.getString('googleServicesPageTitle')])`);
  assert.deepEqual(identityLabels, ['You and Blynx', 'Services and connections']);
  await waitFor(page, `window.blynxFind('#about-menu')?.querySelector('cr-icon')?.shadowRoot?.querySelector('path')?.getAttribute('d')?.startsWith('M2 12C5 4')`);
  assert.equal(await evaluate(page, `(() => {
    const icon = window.blynxFind('#about-menu').querySelector('cr-icon');
    icon.style.setProperty('--iron-icon-fill-color', 'rgb(11, 22, 33)');
    const fill = getComputedStyle(icon.shadowRoot.querySelector('path')).fill;
    icon.style.removeProperty('--iron-icon-fill-color');
    return fill;
  })()`), 'rgb(11, 22, 33)');
  report.checks.push('Settings uses Blynx identity labels and the eye glyph inherits interface icon colors');
  const setupCall = action => evaluate(page, `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('blynxOnboarding', ${JSON.stringify(action)}))`);
  const initialSetup = await setupCall('get');
  assert.equal(initialSetup.editable, true);
  assert.equal(initialSetup.step, output.includes('native-smoke') ? 1 : 0);
  assert.equal((await setupCall('unknown')).saved, false);
  if (initialSetup.step === 0) {
    await waitFor(page, `!!window.blynxFind('#blynxSetupStart')`);
    await evaluate(page, `window.blynxFind('#blynxSetupStart').click()`);
  }
  await waitFor(page, `window.blynxFind('settings-appearance-page').blynxSetup_?.step === 1`);
  assert.equal((await setupCall('back')).saved, false);
  await evaluate(page, `window.blynxFind('#blynxSetupNext').focus()`);
  await page.send('Input.dispatchKeyEvent', {type: 'keyDown', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13});
  await page.send('Input.dispatchKeyEvent', {type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13});
  await waitFor(page, `window.blynxFind('settings-appearance-page').blynxSetup_?.step === 2`);
  assert.equal(await evaluate(page, `window.blynxFind('settings-appearance-page').shadowRoot.activeElement?.id`), 'blynxSetupHeading');
  await fs.writeFile(`${output}/onboarding.png`, Buffer.from((await page.send('Page.captureScreenshot')).data, 'base64'));
  for (const [selector, url, condition] of [
    ['#blynxSetupPasswords', 'chrome://password-manager/settings', `(window.blynxFind('passwords-importer')?.shadowRoot?.querySelector('#selectFileButtonLinkRow')?.getBoundingClientRect().height || 0) > 0`],
    ['#blynxSetupExtensions', 'chrome://extensions/', `!!globalThis.chrome?.developerPrivate?.getExtensionsInfo`],
  ]) {
    const existing = new Set((await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).map(target => target.id));
    const point = await evaluate(page, `(() => {
      const link = window.blynxFind('${selector}');
      link.scrollIntoView({block: 'center'});
      const rect = link.getBoundingClientRect();
      return {x: rect.x + rect.width / 2, y: rect.y + rect.height / 2};
    })()`);
    await page.send('Input.dispatchMouseEvent', {type: 'mousePressed', button: 'left', clickCount: 1, ...point});
    await page.send('Input.dispatchMouseEvent', {type: 'mouseReleased', button: 'left', clickCount: 1, ...point});
    let target;
    const deadline = Date.now() + 15000;
    while (!target && Date.now() < deadline) {
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      target = list.find(item => !existing.has(item.id) && item.url.replace(/^blynx:/, 'chrome:') === url);
      if (!target) await new Promise(resolve => setTimeout(resolve, 100));
    }
    assert(target, `Setup link did not open ${url}: ${JSON.stringify(await (await fetch(`http://127.0.0.1:${port}/json/list`)).json())}`);
    const extra = await connect(target.webSocketDebuggerUrl);
    sockets.push(extra);
    await extra.send('Runtime.enable');
    await evaluate(extra, `window.blynxFind = ${deepFind}`);
    await waitFor(extra, condition);
    await browser.send('Target.closeTarget', {targetId: target.id});
  }
  assert.equal((await setupCall('get')).step, 2);
  report.checks.push('transfer links open the native password CSV importer and extension manager without completing setup');
  await evaluate(page, `window.blynxFind('#blynxSetupImport').click()`);
  await waitFor(page, `!!window.blynxFind('settings-import-data-dialog')`);
  await waitFor(page, `!!window.blynxFind('#browserSelect')?.options.length`);
  assert.equal(await evaluate(page, `location.pathname`), '/importData');
  await fs.writeFile(`${output}/onboarding-import.png`, Buffer.from((await page.send('Page.captureScreenshot')).data, 'base64'));
  await evaluate(page, `window.blynxFind('settings-import-data-dialog').shadowRoot.querySelector('#cancel').click()`);
  await waitFor(page, `location.pathname === '/appearance' && !!window.blynxFind('#blynxSetupNext')`);
  assert.equal((await setupCall('get')).step, 2);
  await evaluate(page, `window.blynxFind('#blynxSetupNext').click()`);
  await waitFor(page, `window.blynxFind('settings-appearance-page').blynxSetup_?.step === 3`);
  assert.equal((await setupCall('next')).saved, false);
  report.checks.push('onboarding advances with heading focus, opens the real browser-data importer and returns after Cancel');
  report.checks.push('onboarding rejects unknown actions and out-of-range transitions without importing data');
  const before = await evaluate(page, getState);
  assert(before.editable);
  await waitFor(page, `!!window.blynxFind('#blynxPreview')`);
  await evaluate(page, `(() => {
    const accent = window.blynxFind('#blynxAccent');
    accent.value = '#C04A72'; accent.dispatchEvent(new Event('input', {bubbles:true}));
    const theme = window.blynxFind('#blynxTheme');
    theme.value = 'dark'; theme.dispatchEvent(new Event('change', {bubbles:true}));
    const style = window.blynxFind('#blynxStyle');
    style.value = 'paper'; style.dispatchEvent(new Event('change', {bubbles:true}));
    window.blynxFind('#blynxLinks').click();
    window.blynxFind('#blynxBookmarks').click();
    window.blynxFind('#blynxVertical').click();
  })()`);
  await waitFor(page, `window.blynxFind('#blynxPreview').classList.contains('dark') && window.blynxFind('.blynx-preview-canvas').classList.contains('paper')`);
  assert.equal(await evaluate(page, `getComputedStyle(window.blynxFind('#blynxPreview')).getPropertyValue('--blynx-preview-accent').trim()`), '#C04A72');
  assert.equal(await evaluate(page, `getComputedStyle(window.blynxFind('.blynx-preview-links')).display`), 'none');
  assert.equal(await evaluate(page, `window.blynxFind('.blynx-preview-window').hasAttribute('data-vertical')`), !before.vertical);
  assert.equal(await evaluate(page, `window.blynxFind('.blynx-preview-bookmarks').hidden`), JSON.parse(before.preset).show_bookmarks_bar);
  assert.equal(await evaluate(page, `new Promise(resolve => {
    const image = new Image();
    image.onload = () => resolve(image.naturalWidth > 0);
    image.onerror = () => resolve(false);
    image.src = getComputedStyle(window.blynxFind('.blynx-preview-canvas .blynx-emblem')).backgroundImage.slice(5, -2);
  })`), true, 'Preview logo must load from the upstream theme source');
  assert.deepEqual(await evaluate(page, getState), before);
  assert.equal(await evaluate(page, `getComputedStyle(window.blynxFind('#blynxPreview figcaption')).color`), await evaluate(page, `getComputedStyle(window.blynxFind('#blynxAccent')).color`), 'Preview caption must use the same readable foreground as the controls');
  report.checks.push('live Settings preview reflects accent/theme/canvas/tab/bookmark/shortcut edits without saving');
  await evaluate(page, `window.blynxFind('#blynxPreview').scrollIntoView({block:'center'})`);
  const previewScreenshot = await page.send('Page.captureScreenshot');
  await fs.writeFile(`${output}/settings-preview.png`, Buffer.from(previewScreenshot.data, 'base64'));

  await evaluate(page, `(() => {
    const accent = window.blynxFind('#blynxAccent');
    accent.value = 'invalid'; accent.dispatchEvent(new Event('input', {bubbles:true}));
  })()`);
  await waitFor(page, `window.blynxFind('#blynxPreview').style.getPropertyValue('--blynx-preview-accent').trim() === '#4F6BED'`);
  assert.deepEqual(await evaluate(page, getState), before);
  await evaluate(page, `window.blynxFind('settings-appearance-page').onBlynxCancelClick_()`);
  await waitFor(page, `window.blynxFind('#blynxAccent').value === ${JSON.stringify(JSON.parse(before.preset).accent_color)}`);
  assert.equal(await evaluate(page, `window.blynxFind('#blynxStyle').value`), before.style);
  assert.equal(await evaluate(page, `window.blynxFind('#blynxVertical').checked`), before.vertical);
  assert.deepEqual(await evaluate(page, getState), before);
  report.checks.push('invalid preview color is bounded and Cancel discards staged edits without changing saved state');
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
  report.expectedSavedAppearance = saved;
  report.checks.push('Settings Apply saves the selected accent, vertical tabs and new-tab canvas');

  // Use actual file input / download plumbing, with only the chooser intercepted
  // so the smoke can select owned fixtures without touching personal files.
  await browser.send('Browser.setDownloadBehavior', {
    behavior: 'allow', downloadPath: `${output}/downloads`, eventsEnabled: true,
  });
  await fs.mkdir(`${output}/preset-fixtures`, {recursive: true});
  const exportPath = `${output}/downloads/blynx-appearance.json`;
  await fs.rm(exportPath, {force: true});
  await evaluate(page, `(() => {
    const input = window.blynxFind('#blynxAccent');
    input.value = 'invalid'; input.dispatchEvent(new Event('input', {bubbles:true}));
    window.blynxFind('#blynxExport').click();
  })()`);
  await waitFor(page, `window.blynxFind('#blynxStatus').textContent.includes('download started')`);
  let exported;
  const exportDeadline = Date.now() + 15000;
  while (Date.now() < exportDeadline) {
    try { exported = await fs.readFile(exportPath, 'utf8'); break; } catch {}
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  assert.equal(exported, saved.preset, 'Export must use saved canonical data, not invalid staged input');
  assert.deepEqual(await evaluate(page, getState), saved);
  await evaluate(page, `window.blynxFind('#blynxCancel').click()`);
  await waitFor(page, `window.blynxFind('#blynxAccent').value === '#3D8D88'`);
  report.checks.push('Export saved preset downloads canonical JSON without saving invalid staged edits');

  async function selectPreset(path) {
    await evaluate(page, `window.blynxFind('settings-appearance-page').blynxStatus_ = ''`);
    const node = await page.send('Runtime.evaluate', {expression: `window.blynxFind('#blynxPresetFile')`});
    await page.send('DOM.setFileInputFiles', {files: [path], objectId: node.result.objectId});
  }
  const importedPreset = {...JSON.parse(saved.preset), theme: 'light', accent_color: '#CE8752', show_bookmarks_bar: false};
  const importPath = `${output}/preset-fixtures/valid.json`;
  await fs.writeFile(importPath, JSON.stringify(importedPreset));
  let chooser;
  page.on('Page.fileChooserOpened', params => { chooser = params; });
  await page.send('Page.setInterceptFileChooserDialog', {enabled: true});
  await page.send('Runtime.evaluate', {expression: `window.blynxFind('#blynxImport').click()`, userGesture: true});
  const chooserDeadline = Date.now() + 5000;
  while (!chooser && Date.now() < chooserDeadline) await new Promise(resolve => setTimeout(resolve, 50));
  assert.equal(chooser?.mode, 'selectSingle', 'Import button must invoke Chromium file chooser');
  await page.send('DOM.setFileInputFiles', {files: [importPath], backendNodeId: chooser.backendNodeId});
  await page.send('Page.setInterceptFileChooserDialog', {enabled: false});
  await waitFor(page, `window.blynxFind('#blynxStatus').textContent.includes('Preset loaded')`);
  assert.equal(await evaluate(page, `window.blynxFind('#blynxAccent').value`), '#CE8752');
  assert.equal(await evaluate(page, `window.blynxFind('#blynxTheme').value`), 'light');
  assert.equal(await evaluate(page, `window.blynxFind('#blynxStyle').value`), saved.style);
  assert.equal(await evaluate(page, `window.blynxFind('#blynxVertical').checked`), saved.vertical);
  assert.deepEqual(await evaluate(page, getState), saved);
  await evaluate(page, `window.blynxFind('#blynxPresetDescription').scrollIntoView({block:'start'})`);
  const importScreenshot = await page.send('Page.captureScreenshot');
  await fs.writeFile(`${output}/settings-import.png`, Buffer.from(importScreenshot.data, 'base64'));
  report.checks.push('Import button opens Chromium chooser and selected file stages validated appearance without saving');

  const staged = await evaluate(page, `window.blynxFind('settings-appearance-page').blynxState_.preset`);
  const invalidFiles = [
    ['unknown-field.json', JSON.stringify({...importedPreset, website: 'https://invalid.example/'})],
    ['future.json', JSON.stringify({...importedPreset, schema_version: 2})],
    ['duplicate.json', '{"schema_version":1,' + JSON.stringify(importedPreset).slice(1)],
    ['bad-color.json', JSON.stringify({...importedPreset, accent_color: '#CE8752;url(x)'})],
    ['oversized.json', ' '.repeat(4097)],
    ['invalid-utf8.json', Buffer.from([0xff, 0xfe])],
    ['empty.json', ''],
    ['malformed.json', '{"theme":'],
  ];
  for (const [name, content] of invalidFiles) {
    const path = `${output}/preset-fixtures/${name}`;
    await fs.writeFile(path, content);
    await selectPreset(path);
    await waitFor(page, `window.blynxFind('#blynxStatus').textContent.includes('No changes were saved')`);
    assert.equal(await evaluate(page, `window.blynxFind('settings-appearance-page').blynxState_.preset`), staged, name);
    assert.deepEqual(await evaluate(page, getState), saved, name);
  }
  report.checks.push('eight invalid preset files are rejected without replacing staged or saved appearance');
  await evaluate(page, `window.blynxFind('#blynxCancel').click()`);
  await waitFor(page, `window.blynxFind('#blynxAccent').value === '#3D8D88'`);
  assert.deepEqual(await evaluate(page, getState), saved);
  report.checks.push('Cancel discards a selected-file import without writing preferences');

  // Race the asynchronous file read against Cancel. The same import handler
  // must discard the late read rather than reinstate edits after cancellation.
  await evaluate(page, `(async () => {
    const element = window.blynxFind('settings-appearance-page');
    const file = new File([${JSON.stringify(JSON.stringify(importedPreset))}], 'delayed.json');
    const bytes = await file.arrayBuffer();
    let release;
    file.arrayBuffer = () => new Promise(resolve => { release = resolve; });
    const pending = element.onBlynxPresetFileChange_({target: {files: [file], value: ''}});
    window.blynxFind('#blynxCancel').click();
    release(bytes);
    await pending;
  })()`);
  assert.equal(await evaluate(page, `window.blynxFind('#blynxAccent').value`), '#3D8D88');
  assert.equal(await evaluate(page, `window.blynxFind('#blynxStatus').textContent.trim()`), '');
  assert.deepEqual(await evaluate(page, getState), saved);
  report.checks.push('Cancel during asynchronous preset reading prevents late imported edits');
  await selectPreset(importPath);
  await waitFor(page, `window.blynxFind('#blynxStatus').textContent.includes('Preset loaded')`);
  await evaluate(page, `window.blynxFind('#blynxApply').click()`);
  await waitFor(page, `window.blynxFind('#blynxStatus').textContent.includes('Saved.')`);
  const importedSaved = await evaluate(page, getState);
  assert.deepEqual(JSON.parse(importedSaved.preset), importedPreset);
  assert.equal(importedSaved.style, saved.style);
  assert.equal(importedSaved.vertical, saved.vertical);
  await selectPreset(exportPath);
  await waitFor(page, `window.blynxFind('#blynxStatus').textContent.includes('Preset loaded')`);
  await evaluate(page, `window.blynxFind('#blynxApply').click()`);
  await waitFor(page, `window.blynxFind('#blynxStatus').textContent.includes('Saved.')`);
  assert.deepEqual(await evaluate(page, getState), saved);
  report.checks.push('Apply saves imported preset and reimporting the exported file restores saved appearance');
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
  await page.send('Page.navigate', {url: 'blynx://settings/help'});
  await waitFor(page, `location.href === 'chrome://settings/help' && !!document.querySelector('settings-ui')`);
  await evaluate(page, `window.blynxFind = ${deepFind}`);
  await waitFor(page, `window.blynxFind('.product-title')?.textContent.trim() === 'Blynx'`);
  assert.equal(await evaluate(page, `import('chrome://resources/js/load_time_data.js').then(m => m.loadTimeData.getString('aboutPageTitle'))`), 'About Blynx');
  const aboutScreenshot = await page.send('Page.captureScreenshot');
  await fs.writeFile(`${output}/about.png`, Buffer.from(aboutScreenshot.data, 'base64'));
  report.checks.push('blynx://settings/help loads Blynx About identity through the upstream Settings page');
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
  const navigation = await page.send('Page.getNavigationHistory');
  await page.send('Page.navigateToHistoryEntry', {entryId: navigation.entries[navigation.currentIndex - 1].id});
  await waitFor(page, `document.title === 'Blynx local smoke'`);
  await page.send('Page.navigateToHistoryEntry', {entryId: navigation.entries[navigation.currentIndex].id});
  await waitFor(page, `document.title === 'Second local page'`);
  report.checks.push('Back and Forward restore the correct local pages');
  await evaluate(page, `localStorage.setItem('blynxIsolation', 'regular')`);
  const {browserContextId} = await browser.send('Target.createBrowserContext');
  let contextFailed = false;
  try {
    const privateTab = await browser.send('Target.createTarget', {url: fixture, browserContextId});
    const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    const privatePage = await connect(targets.find(target => target.id === privateTab.targetId).webSocketDebuggerUrl);
    sockets.push(privatePage);
    await privatePage.send('Runtime.enable');
    await waitFor(privatePage, `document.title === 'Blynx local smoke'`);
    assert.equal(await evaluate(privatePage, `localStorage.getItem('blynxIsolation')`), null);
    await evaluate(privatePage, `localStorage.setItem('blynxIsolation', 'private')`);
    assert.equal(await evaluate(page, `localStorage.getItem('blynxIsolation')`), 'regular');
    report.checks.push('private browser context cannot read or replace regular site storage');
    await privatePage.send('Page.navigate', {url: 'blynx://settings/appearance'});
    await waitFor(privatePage, `location.href === 'chrome://settings/appearance' && !!document.querySelector('settings-ui')`);
    const privateState = await evaluate(privatePage, getState);
    assert.equal(privateState.editable, false);
    const privateSetup = await evaluate(privatePage, `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('blynxOnboarding', 'start'))`);
    assert.equal(privateSetup.editable, false);
    assert.equal(privateSetup.saved, false);
    report.checks.push('private Settings refuses onboarding progress writes');
    const privateValidate = await evaluate(privatePage, `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('validateBlynxAppearancePreset', ${JSON.stringify(saved.preset)}))`);
    assert.equal(privateValidate.valid, false);
    const privateExport = await evaluate(privatePage, `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('exportBlynxAppearancePreset'))`);
    assert.equal(privateExport.valid, false);
    const privateApply = await evaluate(privatePage, `import('chrome://resources/js/cr.js').then(m => m.sendWithPromise('applyBlynxAppearance', ${JSON.stringify(saved.preset)}, false, 'paper', false))`);
    assert.equal(privateApply.saved, false);
    report.checks.push('private Settings refuses preset validation/export and appearance writes through native guards');
    const privateSearch=await evaluate(privatePage, `import('chrome://resources/js/cr.js').then(m=>m.sendWithPromise('getBlynxSearchAppearance'))`);
    assert.equal(privateSearch.editable,false);
    const privateSearchSave=await evaluate(privatePage, `import('chrome://resources/js/cr.js').then(m=>m.sendWithPromise('applyBlynxSearchAppearance',${JSON.stringify(report.expectedSearchAppearance)}))`);
    assert.equal(privateSearchSave.saved,false);
    report.checks.push('private Settings refuses search appearance writes');

  } catch (error) {
    contextFailed = true;
    throw error;
  } finally {
    try {
      await browser.send('Target.disposeBrowserContext', {browserContextId});
    } catch (error) {
      if (!contextFailed) throw error;
      report.cleanupError = String(error);
    }
  }
  await page.send('Page.navigate', {url: 'blynx://bookmarks/'});
  await waitFor(page, `!!globalThis.chrome?.bookmarks?.create`);
  const bookmark = await evaluate(page, `chrome.bookmarks.create({title:'Blynx fixture bookmark', url:${JSON.stringify(fixture)}})`);
  assert.equal((await evaluate(page, `chrome.bookmarks.search({url:${JSON.stringify(fixture)}})`)).length, 1);
  await page.send('Page.reload');
  await waitFor(page, `!!globalThis.chrome?.bookmarks?.get`);
  assert.equal((await evaluate(page, `chrome.bookmarks.get(${JSON.stringify(bookmark.id)})`))[0].title, 'Blynx fixture bookmark');
  await evaluate(page, `chrome.bookmarks.remove(${JSON.stringify(bookmark.id)})`);
  assert.equal((await evaluate(page, `chrome.bookmarks.search({url:${JSON.stringify(fixture)}})`)).length, 0);
  report.checks.push('bookmark create/search/reload/delete use the upstream bookmark service');
  report.expectedBookmark = await evaluate(page, `chrome.bookmarks.create({title:'Blynx restart fixture', url:${JSON.stringify(fixture)}})`);
  await page.send('Page.navigate', {url: 'blynx://history/'});
  await waitFor(page, `!!document.querySelector('history-app')`);
  report.checks.push('blynx://history loads the upstream history UI');
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
  }
  assert.deepEqual(report.pageErrors, [], 'Unexpected JavaScript page errors');
  delete report.error;
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
