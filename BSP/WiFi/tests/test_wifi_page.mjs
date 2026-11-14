import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../../../app/web/wifi_setup_page.c', import.meta.url), 'utf8');
const strings = source.match(/const char\s+wifi_setup_page\[\]\s*=\s*([\s\S]*?);\s*const size_t/)[1];
const html = [...strings.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(match => JSON.parse(match[0])).join('');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
const requests = [];
let state = 'waiting';
let session = 456;
let error = 'none';
let offline = false;
let postStatus = 202;
function element(id) {
  if (!elements.has(id)) elements.set(id, { value: '', textContent: '', disabled: true, type: 'password', addEventListener() {} });
  return elements.get(id);
}
const context = vm.createContext({
  URLSearchParams, AbortController, TextEncoder,
  document: { getElementById: element },
  setInterval() {}, setTimeout() { return 1; }, clearTimeout() {},
  fetch: async (url, options = {}) => {
    requests.push({ url, options });
    if (offline) throw new Error('offline');
    if (options.method === 'POST') {
      assert.equal(url, '/api/wifi/config');
      return { ok: postStatus === 202, status: postStatus };
    }
    assert.equal(url, '/api/wifi/status');
    return { ok: true, json: async () => ({ session, state, error }) };
  },
});
vm.runInContext(script, context);
for (let index = 0; index < 20; ++index) await Promise.resolve();
assert.equal(requests.length, 1, 'initial status must obtain the current setup session');
assert.equal(element('submit').disabled, false);
element('ssid').value = '家中+WiFi';
element('password').value = 'a+b&c=d%';
await vm.runInContext('submitWifi()', context);
const post = requests.find(request => request.options.method === 'POST');
assert.equal(post.options.headers['X-Clock-Request'], '1');
assert.match(post.options.headers['Content-Type'], /application\/x-www-form-urlencoded/);
assert.equal(new URLSearchParams(post.options.body).get('ssid'), '家中+WiFi');
assert.equal(new URLSearchParams(post.options.body).get('session'), '456');
assert.equal(element('password').value, '');
assert.equal(element('submit').disabled, true);
assert.doesNotMatch(element('status').textContent, /已保存/);
offline = true;
await vm.runInContext('loadStatus()', context);
assert.match(element('status').textContent, /重新连接.*热点/);
assert.doesNotMatch(element('status').textContent, /已保存|保存失败/);
offline = false;
for (const current of ['connecting', 'verifying', 'saving']) {
  state = current;
  await vm.runInContext('loadStatus()', context);
  assert.equal(element('submit').disabled, true);
  assert.doesNotMatch(element('status').textContent, /已保存/);
}
state = 'failed'; error = 'storage';
await vm.runInContext('loadStatus()', context);
assert.match(element('status').textContent, /保存失败/);
assert.equal(element('submit').disabled, false);
element('password').value = '12345678';
postStatus = 409;
await vm.runInContext('submitWifi()', context);
assert.doesNotMatch(element('status').textContent, /已保存/);
await vm.runInContext('loadStatus()', context);
element('ssid').value = '家'.repeat(11);
const previousPosts = requests.filter(request => request.options.method === 'POST').length;
await vm.runInContext('submitWifi()', context);
assert.match(element('status').textContent, /32/);
assert.equal(requests.filter(request => request.options.method === 'POST').length, previousPosts);
element('password').type = 'password';
vm.runInContext('togglePassword()', context);
assert.equal(element('password').type, 'text');
state = 'saved';
await vm.runInContext('loadStatus()', context);
assert.match(element('status').textContent, /配置已保存/);
assert.equal(element('submit').disabled, true);
offline = true;
await vm.runInContext('loadStatus()', context);
assert.match(element('status').textContent, /配置已保存/);
offline = false; session = 457; state = 'waiting';
await vm.runInContext('loadStatus()', context);
assert.equal(element('submit').disabled, false);
assert.doesNotMatch(element('status').textContent, /已保存/);
let finishGet;
let gets = 0;
context.fetch = () => { ++gets; return new Promise(resolve => { finishGet = resolve; }); };
const first = vm.runInContext('loadStatus()', context);
const overlapping = vm.runInContext('loadStatus()', context);
assert.equal(gets, 1);
finishGet({ ok: true, json: async () => ({ session: 457, state: 'waiting' }) });
await first; await overlapping;
let timeout;
let cleared = 0;
context.setTimeout = callback => { timeout = callback; return 1; };
context.clearTimeout = () => { ++cleared; };
context.fetch = async (_url, options) => ({
  ok: true,
  json: () => new Promise((_resolve, reject) => options.signal.addEventListener('abort', () => reject(new Error('timeout')))),
});
const stalled = vm.runInContext('loadStatus()', context);
for (let index = 0; index < 10; ++index) await Promise.resolve();
assert.equal(cleared, 0, 'status timeout must include the response body');
timeout();
await stalled;
assert.match(element('status').textContent, /重新连接.*热点/);
assert.doesNotMatch(html, /localStorage|sessionStorage|https?:\/\/|<link|src=/);
const previewDirectory = new URL('../../../build/wifi-validation/', import.meta.url);
mkdirSync(previewDirectory, { recursive: true });
writeFileSync(new URL('wifi-setup.html', previewDirectory), html);
console.log('Wi-Fi browser session, reconnect, validation and truthful status tests passed');
