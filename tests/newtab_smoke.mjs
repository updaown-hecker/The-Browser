import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {createServer} from 'node:http';
const [port, output] = process.argv.slice(2);
const report = {passed:false, checks:[], pageErrors:[]};
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
const {targetId} = await browser.send('Target.createTarget', {url:'about:blank'});
const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
const client = await connect(targets.find(t=>t.id===targetId).webSocketDebuggerUrl);
const check = text => report.checks.push(text);
const engines = `import('chrome://resources/js/cr.js').then(m=>m.sendWithPromise('getSearchEnginesList'))`;
async function settings() {
  await client.send('Page.navigate',{url:'chrome://settings/searchEngines'});
  await waitFor(client, `!!document.querySelector('settings-ui')`);
  return evaluate(client, engines);
}
async function addProvider(name, keyword, url) {
  await evaluate(client, `chrome.send('searchEngineEditStarted',[0]);chrome.send('searchEngineEditCompleted',${JSON.stringify([name,keyword,url])});`);
  await waitFor(client, `${engines}.then(list=>[...list.defaults,...list.actives,...list.others].some(e=>e.name===${JSON.stringify(name)}))`);
  const list = await evaluate(client, engines);
  return [...list.defaults,...list.actives,...list.others].find(e=>e.name===name);
}
async function choose(provider) {
  await evaluate(client, `chrome.send('setDefaultSearchEngine',[${provider.id},2,false])`);
  await waitFor(client, `${engines}.then(list=>[...list.defaults,...list.actives,...list.others].some(e=>e.id===${provider.id} && e.default))`);
}
async function openNewTab() {
  await client.send('Page.navigate',{url:'chrome://newtab/'});
  await waitFor(client, `!!document.querySelector('#browserFinalQuery')`);
  await waitFor(client, `getComputedStyle(document.querySelector('#content')).backgroundImage.includes('gradient')`);
}
const requests = [];
const fixtureSockets = new Set();
const fixture = createServer((req,res) => {
  requests.push(req.url);
  res.writeHead(200, {'Content-Type':'text/html; charset=utf-8'});
  res.end('<!doctype html><title>Search fixture</title><p id="result">Search received</p>');
});
fixture.on('connection', socket=>{fixtureSockets.add(socket);socket.on('close',()=>fixtureSockets.delete(socket));});
await new Promise(resolve=>fixture.listen(0,'127.0.0.1',resolve));
const fixtureOrigin = `http://127.0.0.1:${fixture.address().port}`;
async function waitForTargetURL(prefix) {
  const end=Date.now()+30000;
  while(Date.now()<end) {
    const {targetInfo}=await browser.send('Target.getTargetInfo',{targetId});
    if(targetInfo.url.startsWith(prefix)) return targetInfo.url;
    await new Promise(resolve=>setTimeout(resolve,100));
  }
  throw new Error('Native search did not navigate to '+prefix);
}
try {
  await client.send('Page.enable');
  await client.send('Runtime.enable');
  await client.send('Emulation.setDeviceMetricsOverride',{width:1280,height:800,deviceScaleFactor:1,mobile:false});
  await client.send('Page.navigate',{url:'chrome://version/'});
  await waitFor(client, `document.querySelector('#logo img')?.complete && document.querySelector('#logo img').naturalWidth > 0`);
  assert.equal(await evaluate(client, `document.querySelector('#logo img').alt`),'Blynx logo');
  assert((await evaluate(client, `document.querySelector('#inner').textContent`)).includes('Blynx'));
  assert((await evaluate(client, `document.querySelector('#copyright').textContent`)).includes('Chromium Authors'));
  await client.send('Page.captureScreenshot',{format:'png'}).then(r=>fs.writeFile(`${output}/version.png`,Buffer.from(r.data,'base64')));
  check('version page uses Blynx alt text and product wordmark while retaining upstream copyright');
  await client.send('Page.navigate',{url:'chrome://new-tab-page/'});
  await waitFor(client, `document.querySelector('ntp-app')?.shadowRoot?.querySelector('#browserFinalBrand')`);
  await waitFor(client, `getComputedStyle(document.querySelector('ntp-app').shadowRoot.querySelector('#browserFinalBrand')).display==='grid'`);
  await client.send('Page.captureScreenshot',{format:'png'}).then(r=>fs.writeFile(`${output}/newtab-google.png`,Buffer.from(r.data,'base64')));
  check('Google-backed built-in new tab loads the shared Blynx canvas stylesheet');
  const initial = await settings();
  const original = [...initial.defaults,...initial.actives,...initial.others].find(e=>e.default);
  // This uses Chromium Settings in the disposable profile. The wrapper blocks
  // Brave DNS resolution; successful response handling uses a loopback fixture.
  const brave = await addProvider('Brave Search fixture','blynx-brave-fixture','https://search.brave.com/search?q=%s');
  await choose(brave);
  await openNewTab();
  assert((await evaluate(client, `document.querySelector('#browserFinalSearch label').textContent`)).includes('Brave Search fixture'));
  assert.equal(await evaluate(client, `document.querySelector('#browserFinalQuery').labels.length`),1);
  assert.equal(await evaluate(client, `document.documentElement.scrollWidth > innerWidth`),false);
  await client.send('Page.captureScreenshot',{format:'png'}).then(r=>fs.writeFile(`${output}/newtab-brave.png`,Buffer.from(r.data,'base64')));
  check('switching the real default provider to Brave keeps Blynx branding, visible labeled search and shortcuts');
  await evaluate(client, `document.querySelector('#browserFinalQuery').value='blynx privacy & tabs';document.querySelector('#browserFinalSearch').requestSubmit()`);
  const braveURL = new URL(await waitForTargetURL('https://search.brave.com/search?'));
  assert.equal(braveURL.searchParams.get('q'),'blynx privacy & tabs');
  check('native submission uses the selected Brave template and correctly encodes the query');
  await settings();
  const second = await addProvider('Second search fixture','blynx-second-fixture',fixtureOrigin+'/find?terms=%s');
  await choose(second);
  await openNewTab();
  await client.send('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:false});
  await waitFor(client, `innerWidth===390`);
  assert.equal(await evaluate(client, `document.documentElement.scrollWidth > innerWidth`),false);
  await client.send('Page.captureScreenshot',{format:'png'}).then(r=>fs.writeFile(`${output}/newtab-narrow.png`,Buffer.from(r.data,'base64')));
  await evaluate(client, `document.querySelector('#browserFinalQuery').value='second engine';document.querySelector('#browserFinalSearch').requestSubmit()`);
  await waitFor(client, `!!document.querySelector('#result')`);
  assert.equal(new URL(requests.find(url=>url.startsWith('/find?')),fixtureOrigin).searchParams.get('terms'),'second engine');
  check('a second non-Google provider works and the 390px canvas has no horizontal overflow');
  await settings();
  await choose(original);
  assert.deepEqual(report.pageErrors,[]);
  report.passed=true;
} catch (error) {
  report.error=String(error);
  report.requests=requests;
  report.current=await evaluate(client, `({url:location.href,ready:document.readyState,text:document.body.innerText.slice(0,800)})`).catch(String);
  throw error;
} finally {
  await fs.writeFile(`${output}/results.json`,JSON.stringify(report,null,2)+'\n');
  fixture.close();
  for(const socket of fixtureSockets) socket.destroy();
  client.close();
  await browser.send('Target.closeTarget',{targetId}).catch(()=>{});
  browser.close();
}
console.log(JSON.stringify(report,null,2));
