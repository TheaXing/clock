import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../../../app/web/web_page.c', import.meta.url), 'utf8');
const strings = source.match(/const char\s+clock_web_page\[\]\s*=\s*([\s\S]*?);\s*const size_t/)[1];
const html = [...strings.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(match => JSON.parse(match[0])).join('');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
const requests = [];
let stage = 'idle';
let session = 123;
let version = 17;
let postStatus = 202;
let confirmed = 0;

function element(id) {
  if (!elements.has(id)) {
    elements.set(id, {
      value: '', textContent: '', innerHTML: '', dataset: {}, checked: false, disabled: false,
      hidden: false, addEventListener() {}, showModal() {}, close() {},
    });
  }
  return elements.get(id);
}

const days = Array.from({ length: 7 }, (_, index) => ({ checked: false, disabled: false, dataset: { day: String(index) }, addEventListener() {} }));
const context = vm.createContext({
  URLSearchParams, AbortController, Date,
  document: { getElementById: element, querySelectorAll: selector => selector === '[data-day]' ? days : [] },
  window: { confirm: () => { ++confirmed; return true; } },
  setInterval: () => 1, setTimeout: () => 1, clearTimeout() {},
  fetch: async (url, options = {}) => {
    requests.push({ url, options });
    if (url === '/api/alarms') {
      return { ok: true, status: 200, json: async () => ({ session, version: 1, ota_supported: true, storage_ok: true,
        alarms: Array.from({ length: 8 }, (_, index) => ({ id: index + 1, repeat_mask: 127, hour: 7, minute: 0, ring_seconds: 60, snooze_seconds: 300 })) }) };
    }
    assert.equal(url, '/api/ota');
    if (options.method === 'POST') {
      if (postStatus === 202) stage = 'queued';
      return { ok: postStatus < 400, status: postStatus, json: async () => ({ id: 1, state: stage }) };
    }
    return { ok: true, status: 200, json: async () => ({ enabled: true, session, version, identity: 'test-package', id: 1, state: stage, error: 3 }) };
  },
});
vm.runInContext(script, context);
for (let index = 0; index < 60; ++index) await Promise.resolve();
assert.equal(element('otaStart').disabled, false);
assert.match(element('otaVersion').textContent, /17/);
element('otaToken').value = 'ab'.repeat(32);
await vm.runInContext('startOta()', context);
assert.equal(confirmed, 1);
assert.equal(element('otaToken').value, '');
assert.equal(element('otaStart').disabled, true);
assert.doesNotMatch(element('otaStatus').textContent, /已保存|即将重启/);
const posts = requests.filter(request => request.options.method === 'POST');
assert.equal(posts.length, 1);
assert.equal(posts[0].options.headers['X-Clock-Request'], '1');
assert.equal(new URLSearchParams(posts[0].options.body).get('session'), '123');
stage = 'failed';
await vm.runInContext('loadOta()', context);
assert.match(element('otaStatus').textContent, /未保存/);
assert.equal(element('otaStart').disabled, false);
postStatus = 403;
element('otaToken').value = 'ab'.repeat(32);
await vm.runInContext('startOta()', context);
assert.match(element('otaStatus').textContent, /无效/);
assert.equal(element('otaToken').value, '');
stage = 'saved';
await vm.runInContext('loadOta()', context);
assert.match(element('otaStatus').textContent, /已保存.*重启/);
assert.equal(element('otaStart').disabled, true);
stage = 'idle';
session = 456;
version = 18;
await vm.runInContext('loadOta()', context);
assert.match(element('otaVersion').textContent, /18/);
assert.match(element('otaStatus').textContent, /重新连接.*核对/);
assert.equal(element('otaStart').disabled, false);
assert.doesNotMatch(html, /localStorage|sessionStorage|CLOCK_OTA_(?:KEY|ADMIN_TOKEN)_HEX/);
let pendingGets = 0;
let finishGet;
context.fetch = () => {
  ++pendingGets;
  return new Promise(resolve => { finishGet = resolve; });
};
const firstPoll = vm.runInContext('loadOta()', context);
const overlappingPoll = vm.runInContext('loadOta()', context);
assert.equal(pendingGets, 1, 'slow status requests must not exhaust the five ESP connections');
finishGet({ ok: true, status: 200, json: async () => ({ enabled: true, session, version, state: 'idle' }) });
await firstPoll;
await overlappingPoll;
console.log('OTA browser behavior passed');
