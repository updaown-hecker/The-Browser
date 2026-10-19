import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
const [port, output] = process.argv.slice(2);
const report = {passed: false, checks: [], pageErrors: []};
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

const metadata = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
const browser = await connect(metadata.webSocketDebuggerUrl);
const {targetId} = await browser.send('Target.createTarget', {url: 'about:blank'});
const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
const client = await connect(targets.find(t => t.id === targetId).webSocketDebuggerUrl);
const fixtureSetup = `(() => {
  document.body.replaceChildren();
  document.body.style.cssText='font:18px sans-serif;background:#17191d;color:#eee;padding:40px';
  const heading=document.createElement('h1');heading.textContent='Chrome Web Store fixture';
  const add=document.createElement('button');add.id='add';add.setAttribute('aria-label','Add to Chrome');
  const span=document.createElement('span');span.textContent='Add to Chrome';add.append(span);
  add.style.cssText='padding:14px;border-radius:22px';
  const disabled=document.createElement('button');disabled.id='disabled';disabled.disabled=true;disabled.textContent='Add to Chromium';
  const description=document.createElement('p');description.id='description';description.textContent='Add to Chrome';
  window.clicks=0;add.addEventListener('click',()=>window.clicks++);
  document.body.append(heading,add,disabled,description);
})()`;
const html = `<!doctype html><meta charset="utf-8"><title>Branding fixture</title>
<style>body{font:18px sans-serif;background:#17191d;color:#eee;padding:40px}button{padding:14px;border-radius:22px}</style>
<h1>Chrome Web Store fixture</h1><button id="add" aria-label="Add to Chrome"><span>Add to Chrome</span></button>
<button id="disabled" disabled>Add to Chromium</button><p id="description">Add to Chrome</p>
<script>window.clicks=0;document.querySelector('#add').addEventListener('click',()=>window.clicks++);</script>`;
client.on('Fetch.requestPaused', request => {
  if (request.request.url.startsWith('https://chromewebstore.google.com/')) {
    client.send('Fetch.continueRequest', {requestId: request.requestId}).catch(error => report.pageErrors.push(String(error)));
    return;
  }
  client.send('Fetch.fulfillRequest', {
    requestId: request.requestId, responseCode: 200,
    responseHeaders: [{name:'Content-Type',value:'text/html; charset=utf-8'}],
    body: Buffer.from(html).toString('base64'),
  }).catch(error => report.pageErrors.push(String(error)));
});
const promotionSetup = `(() => {
  const banner=document.createElement('div');banner.className='gSrP5d';banner.id='switch-banner';
  const text=document.createElement('div');text.className='UWpb5';text.textContent='Switch to Chrome to install extensions and themes';
  const install=document.createElement('button');install.textContent='Install Chrome';install.setAttribute('aria-label',text.textContent+' Install Chrome');
  banner.append(text,install);
  const dialog=document.createElement('div');dialog.setAttribute('role','dialog');dialog.id='switch-dialog';
  const heading=document.createElement('h2');heading.id='promo-header';heading.textContent='Switch to Chrome?';
  const message=document.createElement('p');message.className='XTn9Fc';message.textContent='Google recommends using Chrome when using extensions and themes.';
  const dismiss=document.createElement('button');dismiss.setAttribute('jsname','gQ2Xie');dismiss.textContent='No thanks';
  window.promoDismissals=0;dismiss.addEventListener('click',()=>window.promoDismissals++);
  dialog.append(heading,message,dismiss);
  const security=document.createElement('div');security.className='gSrP5d';security.id='security-warning';
  const warning=document.createElement('div');warning.className='UWpb5';warning.textContent='This extension is blocked by your administrator';security.append(warning);
  const permission=document.createElement('div');permission.setAttribute('role','dialog');permission.id='permission-dialog';permission.textContent='Extension permissions';
  document.body.append(banner,dialog,security,permission);
})()`;
async function navigate(url) {
  if (url.startsWith('https://chromewebstore.google.com/')) {
    if (!await evaluate(client, "location.origin === 'https://chromewebstore.google.com'")) {
      // A real HTTPS response retains upstream parsed X-Frame-Options checks.
      await client.send('Page.navigate', {url: 'https://chromewebstore.google.com/robots.txt'});
      await waitFor(client, "location.origin === 'https://chromewebstore.google.com' && document.readyState === 'complete'");
      await new Promise(resolve => setTimeout(resolve, 250));
    }
    await evaluate(client, `history.replaceState({},'',${JSON.stringify(url)});${fixtureSetup}`);
  } else {
    await client.send('Page.navigate', {url});
  }
  await waitFor(client, `location.href === ${JSON.stringify(url)} && !!document.querySelector('#add')`);
  await new Promise(resolve => setTimeout(resolve, 250));
}

try {
  await client.send('Emulation.setDeviceMetricsOverride', {width:1280,height:800,deviceScaleFactor:1,mobile:false});
  await client.send('Page.enable');
  await client.send('Runtime.enable');
  await client.send('Fetch.enable', {patterns:[{urlPattern:'*',resourceType:'Document',requestStage:'Request'}]});
  await navigate('https://chromewebstore.google.com/detail/blynx-test/fixture');
  await waitFor(client, `document.querySelector('#add').textContent === 'Add to Blynx'`);
  assert.equal(await evaluate(client, `document.querySelector('#add').getAttribute('aria-label')`), 'Add to Blynx');
  assert.equal(await evaluate(client, `document.querySelector('#disabled').textContent`), 'Add to Blynx');
  assert.equal(await evaluate(client, `document.querySelector('#disabled').disabled`), true);
  assert.equal(await evaluate(client, `document.querySelector('#description').textContent`), 'Add to Chrome');
  await evaluate(client, `document.querySelector('#add').click()`);
  assert.equal(await evaluate(client, 'window.clicks'), 1);
  report.checks.push('native renderer hook changes visible/accessibility labels while preserving buttons, listeners, disabled state and descriptions');
  await evaluate(client, `{const button=document.createElement('button');button.id='dynamic';button.textContent='Add to Google Chrome';document.body.append(button)}`);
  await waitFor(client, `document.querySelector('#dynamic').textContent === 'Add to Blynx'`);
  await evaluate(client, `document.querySelector('#add').setAttribute('aria-label','Add to Chrome')`);
  await waitFor(client, `document.querySelector('#add').getAttribute('aria-label') === 'Add to Blynx'`);
  report.checks.push('asynchronous Store buttons and accessible-label updates receive Blynx branding');
  await evaluate(client, promotionSetup);
  await waitFor(client, `document.querySelector('#switch-banner').hidden && document.querySelector('#switch-dialog').hidden`);
  assert.equal(await evaluate(client, `window.promoDismissals`), 1);
  assert.equal(await evaluate(client, `document.querySelector('#switch-dialog').inert`), true);
  assert.equal(await evaluate(client, `getComputedStyle(document.querySelector('#switch-banner')).display`), 'none');
  assert.equal(await evaluate(client, `document.querySelector('#security-warning').hidden`), false);
  assert.equal(await evaluate(client, `document.querySelector('#permission-dialog').hidden`), false);
  assert.equal(await evaluate(client, `document.querySelector('#disabled').disabled`), true);
  report.checks.push('only exact Chrome acquisition banner/dialog are hidden; site dismissal runs and security/permissions/disabled install controls remain');
  await client.send('Page.captureScreenshot', {format:'png'}).then(r => fs.writeFile(`${output}/store-label.png`, Buffer.from(r.data,'base64')));
  await navigate('https://chromewebstore.google.com/category/extensions');
  assert.equal(await evaluate(client, `document.querySelector('#add').textContent`), 'Add to Chrome');
  await evaluate(client, promotionSetup);
  await waitFor(client, `document.querySelector('#switch-banner').hidden && document.querySelector('#switch-dialog').hidden`);
  report.checks.push('Chrome promotions are suppressed outside detail routes and after dynamic rendering');
  await evaluate(client, `history.pushState({},'', '/detail/spa/fixture');document.querySelector('#add span').textContent='Add to Chromium'`);
  await waitFor(client, `document.querySelector('#add').textContent === 'Add to Blynx'`);
  report.checks.push('only detail routes are relabeled; same-document Store navigation is supported');
  for (const url of ['https://chromewebstore.google.com.example.com/detail/fixture',
                     'https://example.com/detail/fixture']) {
    await navigate(url);
    assert.equal(await evaluate(client, `document.querySelector('#add').textContent`), 'Add to Chrome', url);
  }
  report.checks.push('lookalike hosts and other websites are untouched');
  await client.send('Fetch.disable');
  await client.send('Emulation.setEmulatedMedia', {features:[{name:'prefers-color-scheme',value:'dark'}]});
  await client.send('Page.navigate', {url:'chrome://extensions/'});
  await waitFor(client, `document.querySelector('extensions-manager')?.shadowRoot?.querySelector('extensions-toolbar')?.shadowRoot?.querySelector('cr-toolbar')?.shadowRoot?.querySelector('#product-logo')?.complete`);
  const icon = `document.querySelector('extensions-manager').shadowRoot.querySelector('extensions-toolbar').shadowRoot.querySelector('cr-toolbar').shadowRoot.querySelector('#product-logo')`;
  await waitFor(client, `${icon}.currentSrc.includes('chrome_logo_dark.svg')`);
  assert.equal(await evaluate(client, `${icon}.naturalWidth`), 24);
  assert.equal(await evaluate(client, `${icon}.naturalHeight`), 24);
  await client.send('Page.captureScreenshot', {format:'png'}).then(r=>fs.writeFile(`${output}/extensions-dark.png`,Buffer.from(r.data,'base64')));
  report.checks.push('actual dark-mode Extensions toolbar loads the Blynx eye resource');
  assert.deepEqual(report.pageErrors, []);
  report.passed = true;
} catch (error) {
  report.error = String(error);
  throw error;
} finally {
  await fs.writeFile(`${output}/results.json`, JSON.stringify(report,null,2)+'\n');
  client.close();
  if (browser) await browser.send('Target.closeTarget', {targetId}).catch(() => {});
  browser.close();
}
console.log(JSON.stringify(report,null,2));
