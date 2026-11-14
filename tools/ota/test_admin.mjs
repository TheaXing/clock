import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash, createHmac } from 'node:crypto';
import * as fs from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { request } from 'node:http';
import { spawn } from 'node:child_process';
import { createPackage } from './protocol.mjs';
import { openReleaseStore } from './release-store.mjs';
import { createAdminServer } from './admin-server.mjs';
import { createOtaServer } from './server.mjs';

// Public, isolated fixture credentials; never read any user's private configuration.
const key = Buffer.from('000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f', 'hex');
const token = 'ab'.repeat(32);
const auth = { Authorization: `Bearer ${token}` };

function fixture(version = 2, fill = 0xa5, size = 512) {
  const image = Buffer.alloc(size, fill);
  image.writeUInt32LE(0x20020000, 0);
  image.writeUInt32LE(0x08010189, 4);
  return createPackage(image, key, version);
}

async function directoryFor(t) {
  const directory = await fs.mkdtemp(join(tmpdir(), 'clock-ota-admin-'));
  t.after(() => fs.rm(directory, { recursive: true, force: true }));
  return directory;
}

async function storeFor(t, options = {}) {
  return openReleaseStore({ directory: await directoryFor(t), key, ...options });
}

async function listen(t, server) {
  await new Promise((done, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', done);
  });
  t.after(() => new Promise(done => server.close(done)));
  return server.address().port;
}

async function http(port, path = '/api/releases', { method = 'GET', headers = auth, body } = {}) {
  return new Promise((done, reject) => {
    const req = request({ host: '127.0.0.1', port, path, method, headers }, res => {
      const chunks = [];
      res.on('data', chunk => chunks.push(chunk));
      res.on('end', () => {
        const bytes = Buffer.concat(chunks);
        done({ status: res.statusCode, headers: res.headers, bytes, json: () => JSON.parse(bytes.toString()) });
      });
    });
    req.on('error', reject);
    req.end(body);
  });
}

function upload(port, bytes = fixture(), headers = {}) {
  return http(port, '/api/releases', {
    method: 'POST', body: bytes,
    headers: { ...auth, 'Content-Type': 'application/octet-stream', 'Content-Length': bytes.length, ...headers }
  });
}

test('verified upload is staged, copied, deduplicated and persisted without publishing', async t => {
  const directory = await directoryFor(t);
  const store = await openReleaseStore({ directory, key });
  assert.deepEqual(await store.latest(), { available: false });
  const bytes = fixture();
  const original = Buffer.from(bytes);
  const result = await store.upload(bytes);
  assert.equal(result.created, true);
  assert.equal(result.release.version, 2);
  assert.equal(result.release.packageSize, 768);
  assert.equal(result.release.board, 'Clock_CubeMX-STM32F407VE');
  assert.equal(result.release.packageSha256, createHash('sha256').update(original).digest('hex'));
  bytes.fill(0);
  result.release.version = 999;
  const repeated = await store.upload(original);
  assert.equal(repeated.created, false);
  assert.equal(repeated.release.version, 2);
  const reopened = await openReleaseStore({ directory, key });
  const listing = await reopened.list();
  assert.equal(listing.releases.length, 1);
  assert.equal(listing.current, null);
  assert.equal(listing.storage.usedBytes, 768);
  assert.deepEqual((await reopened.read(repeated.release.packageId)).bytes, original);
  assert.deepEqual(await reopened.latest(), { available: false });
});

test('same version with different authenticated bytes conflicts and bad packages leave no accepted record', async t => {
  const store = await storeFor(t);
  await store.upload(fixture());
  await assert.rejects(store.upload(fixture(2, 0x5a)), error => error.status === 409);
  const badBoard = fixture(3);
  badBoard[32] ^= 1;
  createHash('sha256').update(badBoard.subarray(0, 96)).digest().copy(badBoard, 96, 0, 16);
  createHmac('sha256', key).update(badBoard.subarray(0, 192)).digest().copy(badBoard, 192);
  for (const bytes of [Buffer.alloc(10), fixture(3).subarray(0, 700), badBoard, Buffer.alloc(458753)]) {
    await assert.rejects(store.upload(bytes));
  }
  assert.equal((await store.list()).releases.length, 1);
});

test('publication survives restart; concurrent publication stays complete and immutable objects remain readable', async t => {
  const directory = await directoryFor(t);
  const store = await openReleaseStore({ directory, key });
  const a = (await store.upload(fixture(2))).release;
  const b = (await store.upload(fixture(3))).release;
  await store.publish(a.packageId);
  const latest = await store.latest();
  assert.deepEqual(latest, { available: true, version: 2, packageId: a.packageId, header: fixture(2).subarray(0, 256).toString('hex') });
  assert.ok(Buffer.byteLength(JSON.stringify(latest)) < 1024);
  await Promise.all([store.publish(b.packageId), store.publish(a.packageId), store.publish(b.packageId)]);
  const reopened = await openReleaseStore({ directory, key });
  assert.equal((await reopened.list()).current.packageId, b.packageId);
  assert.ok((await reopened.list()).current.publishedAt);
  assert.deepEqual((await reopened.read(a.packageId)).bytes, fixture(2));
  await reopened.publish(a.packageId);
  assert.equal((await reopened.latest()).version, 2);
});

test('failed pointer rename preserves prior durable release, including after reopening', async t => {
  const directory = await directoryFor(t);
  let failPublication = false;
  const fileSystem = { ...fs, rename: async (from, to) => {
    if (failPublication && to === join(directory, 'current.json')) {
      throw Object.assign(new Error('injected disk rename failure'), { code: 'EIO' });
    }
    return fs.rename(from, to);
  } };
  const store = await openReleaseStore({ directory, key, fileSystem });
  const a = (await store.upload(fixture(2))).release;
  const b = (await store.upload(fixture(3))).release;
  await store.publish(a.packageId);
  const pointer = await fs.readFile(join(directory, 'current.json'));
  failPublication = true;
  await assert.rejects(store.publish(b.packageId));
  assert.deepEqual(await fs.readFile(join(directory, 'current.json')), pointer);
  assert.equal((await store.latest()).packageId, a.packageId);
  assert.equal((await (await openReleaseStore({ directory, key })).latest()).packageId, a.packageId);
});

test('failed release-record commit removes only its unaccepted object and returns quota for a later upload', async t => {
  const directory = await directoryFor(t);
  let failRecord = false;
  const fileSystem = { ...fs, rename: async (from, to) => {
    if (failRecord && to.startsWith(join(directory, 'releases'))) {
      throw Object.assign(new Error('injected record commit failure'), { code: 'ENOSPC' });
    }
    return fs.rename(from, to);
  } };
  const store = await openReleaseStore({ directory, key, maxBytes: 1536, fileSystem });
  const a = (await store.upload(fixture(2))).release;
  await store.publish(a.packageId);
  failRecord = true;
  await assert.rejects(store.upload(fixture(3)));
  assert.equal((await store.list()).releases.length, 1);
  assert.equal((await store.list()).storage.usedBytes, 768);
  assert.equal((await store.latest()).version, 2);
  assert.equal((await fs.readdir(join(directory, 'temporary'))).length, 0);
  failRecord = false;
  assert.equal((await store.upload(fixture(4))).created, true);
  assert.equal((await (await openReleaseStore({ directory, key, maxBytes: 1536 })).list()).releases.length, 2);
});

test('quota includes outstanding upload reservations and rejected uploads do not consume storage', async t => {
  const store = await storeFor(t, { maxBytes: 1536 });
  const held = await store.reserveUpload(768);
  await store.upload(fixture(2));
  await assert.rejects(store.upload(fixture(3)), error => error.status === 507);
  held.release();
  await store.upload(fixture(3));
  assert.equal((await store.list()).storage.usedBytes, 1536);
  await assert.rejects(store.upload(fixture(4)), error => error.status === 507);
  assert.equal((await store.upload(fixture(2))).created, false);
});

test('manual corruption cannot masquerade behind old immutable identity or ETag; damaged pointer never selects staged upload', async t => {
  const directory = await directoryFor(t);
  const store = await openReleaseStore({ directory, key });
  const a = (await store.upload(fixture(2))).release;
  await store.publish(a.packageId);
  await fs.writeFile(join(directory, 'objects', `${a.packageSha256}.ota`), fixture(3));
  await assert.rejects(store.read(a.packageId));
  await assert.rejects(store.latest());
  await assert.rejects(openReleaseStore({ directory, key }));
  await fs.writeFile(join(directory, 'objects', `${a.packageSha256}.ota`), fixture(2));
  await fs.writeFile(join(directory, 'current.json'), '{broken');
  await assert.rejects(openReleaseStore({ directory, key }));
});

test('HTTP management requires bearer credentials and exact origin; static whitelist discloses no secrets', async t => {
  const port = await listen(t, createAdminServer({ store: await storeFor(t), adminTokenHex: token }));
  for (const headers of [{}, { Authorization: `Bearer ${'cc'.repeat(32)}` }, { Authorization: `Bearer ${'g'.repeat(64)}` }]) {
    assert.equal((await http(port, '/api/releases', { headers })).status, 401);
  }
  for (const origin of ['http://evil.invalid', 'null', `https://127.0.0.1:${port}`, `http://127.0.0.1:${port}/path`]) {
    assert.equal((await http(port, '/api/releases', { headers: { ...auth, Origin: origin } })).status, 403);
  }
  const allowed = await http(port, '/api/releases', { headers: { ...auth, Origin: `http://127.0.0.1:${port}` } });
  assert.equal(allowed.status, 200);
  assert.equal(allowed.headers['access-control-allow-origin'], undefined);
  for (const path of ['/', '/app.mjs', '/style.css']) {
    const response = await http(port, path, { headers: {} });
    assert.equal(response.status, 200, path);
    assert.equal(response.bytes.includes(Buffer.from(token)), false);
    assert.equal(response.bytes.includes(key), false);
  }
  for (const path of ['/../ota_key_local.json', '/%2e%2e/ota_key_local.json', '/objects/a.ota', '/api/releases?token=x', '/app.mjs?x=1']) {
    assert.equal((await http(port, path)).status, 404, path);
  }
});

test('HTTP staged upload, publication and immutable ranges follow the device discovery contract', async t => {
  const port = await listen(t, createAdminServer({ store: await storeFor(t), adminTokenHex: token }));
  assert.deepEqual((await http(port, '/firmware.ota?latest=1')).json(), { available: false });
  assert.equal((await http(port, '/firmware.ota')).status, 404);
  const staged = await upload(port);
  assert.equal(staged.status, 201);
  const a = staged.json().release;
  assert.equal((await upload(port)).status, 200);
  assert.equal((await upload(port, fixture(2, 0x5a))).status, 409);
  assert.deepEqual((await http(port, '/firmware.ota?latest=1')).json(), { available: false });
  assert.equal((await http(port, `/api/releases/${a.packageId}/publish`, { method: 'POST' })).status, 200);
  assert.equal((await http(port, '/firmware.ota?latest=1')).json().packageId, a.packageId);
  const initial = await http(port, '/firmware.ota', { headers: { Range: 'bytes=0-255' } });
  assert.equal(initial.status, 206);
  assert.equal(initial.headers['content-range'], 'bytes 0-255/768');
  const b = (await upload(port, fixture(3))).json().release;
  await http(port, `/api/releases/${b.packageId}/publish`, { method: 'POST' });
  assert.equal((await http(port, '/firmware.ota', { headers: { 'If-Match': initial.headers.etag } })).status, 412);
  const fixed = await http(port, `/firmware.ota?id=${a.packageId}`, { headers: { Range: 'bytes=256-', 'If-Match': initial.headers.etag } });
  assert.equal(fixed.status, 206);
  assert.equal(fixed.headers['content-length'], '512');
  assert.deepEqual(fixed.bytes, fixture(2).subarray(256));
  const head = await http(port, `/firmware.ota?id=${a.packageId}`, { method: 'HEAD', headers: { Range: 'bytes=-8' } });
  assert.equal(head.status, 206);
  assert.equal(head.headers['content-length'], '8');
  assert.equal(head.bytes.length, 0);
  assert.equal((await http(port, `/firmware.ota?id=${a.packageId}`, { headers: { Range: 'bytes=900-' } })).status, 416);
  for (const query of ['?latest=1&x=1', '?latest=01', `?id=${a.packageId}&latest=1`, `?id=${a.packageId.toUpperCase()}`, '?id=../a', '?latest=%31']) {
    assert.equal((await http(port, `/firmware.ota${query}`)).status, 404, query);
  }
});

test('HTTP refuses wrong type, invalid signature, missing length and oversize without changing current publication', async t => {
  const store = await storeFor(t);
  const a = (await store.upload(fixture())).release;
  await store.publish(a.packageId);
  const port = await listen(t, createAdminServer({ store, adminTokenHex: token }));
  assert.equal((await upload(port, fixture(3), { 'Content-Type': 'text/plain' })).status, 415);
  const bad = fixture(3);
  bad[767] ^= 1;
  assert.equal((await upload(port, bad)).status, 422);
  assert.equal((await upload(port, Buffer.alloc(458753))).status, 413);
  assert.equal((await http(port, '/api/releases', { method: 'POST', headers: { ...auth, 'Content-Type': 'application/octet-stream', 'Transfer-Encoding': 'chunked' }, body: fixture(3) })).status, 411);
  assert.equal((await store.list()).releases.length, 1);
  assert.equal((await store.latest()).version, 2);
});

test('HTTP quota failure preserves the published alias and restart; concurrent publishes only select complete known releases', async t => {
  const directory = await directoryFor(t);
  const store = await openReleaseStore({ directory, key, maxBytes: 1536 });
  const port = await listen(t, createAdminServer({ store, adminTokenHex: token, path: '/clock/update.ota' }));
  const a = (await upload(port, fixture(2))).json().release;
  const b = (await upload(port, fixture(3))).json().release;
  const paths = [a, b, a, b].map(release => `/api/releases/${release.packageId}/publish`);
  const results = await Promise.all(paths.map(path => http(port, path, { method: 'POST' })));
  assert.ok(results.every(result => result.status === 200));
  assert.equal((await upload(port, fixture(4))).status, 507);
  const latest = (await http(port, '/clock/update.ota?latest=1')).json();
  assert.ok([a.packageId, b.packageId].includes(latest.packageId));
  assert.equal((await http(port, '/firmware.ota?latest=1')).status, 404);
  const body = await http(port, '/clock/update.ota');
  assert.equal(body.status, 200);
  assert.deepEqual(body.bytes, fixture(latest.version));
  const reopened = await openReleaseStore({ directory, key, maxBytes: 1536 });
  assert.equal((await reopened.latest()).packageId, latest.packageId);
  assert.equal((await reopened.list()).releases.length, 2);
});

test('slow, interrupted and concurrent uploads release their reservations without accepting partial bytes', async t => {
  const store = await storeFor(t, { maxBytes: 768 * 4 });
  const port = await listen(t, createAdminServer({ store, adminTokenHex: token, uploadDeadlineMs: 150 }));
  const open = () => {
    const req = request({ host: '127.0.0.1', port, path: '/api/releases', method: 'POST', headers: { ...auth, 'Content-Type': 'application/octet-stream', 'Content-Length': 768 } });
    req.on('error', () => {});
    const result = new Promise(done => req.on('response', res => { res.resume(); res.on('end', () => done(res.statusCode)); }));
    req.flushHeaders();
    req.write(fixture().subarray(0, 64));
    return { req, result };
  };
  const a = open();
  const b = open();
  await new Promise(done => setTimeout(done, 35));
  assert.equal((await upload(port)).status, 429);
  a.req.destroy();
  assert.equal(await b.result, 408);
  assert.equal((await store.list()).releases.length, 0);
  assert.equal((await upload(port)).status, 201);
});

test('single-package compatibility server supports only its matching latest and immutable identity', async t => {
  const bytes = fixture();
  const port = await listen(t, createOtaServer({ packageBytes: bytes, key }));
  const latest = (await http(port, '/firmware.ota?latest=1')).json();
  assert.equal(latest.version, 2);
  assert.equal(latest.header, bytes.subarray(0, 256).toString('hex'));
  assert.deepEqual((await http(port, `/firmware.ota?id=${latest.packageId}`)).bytes, bytes);
  assert.equal((await http(port, `/firmware.ota?id=${'0'.repeat(32)}`)).status, 404);
});

test('admin CLI launches with isolated key/data paths, keeps uploads after process restart, and never logs credentials', async t => {
  const directory = await directoryFor(t);
  const keyFile = join(directory, 'fixture-key.json');
  const data = join(directory, 'data');
  await fs.writeFile(keyFile, JSON.stringify({ keyHex: key.toString('hex'), adminTokenHex: token }));
  const child = spawn(process.execPath, [resolve('tools/ota/ota.mjs'), 'admin', '--key-file', keyFile, '--data-dir', data, '--host', '127.0.0.1', '--port', '0'], { stdio: ['ignore', 'pipe', 'pipe'] });
  let output = '';
  t.after(() => {
    if (child.exitCode === null && child.signalCode === null) {
      child.kill();
      return new Promise(done => child.once('exit', done));
    }
  });
  const port = await new Promise((done, reject) => {
    const timeout = setTimeout(() => reject(new Error(`CLI launch timed out: ${output}`)), 5000);
    child.once('exit', () => { clearTimeout(timeout); reject(new Error(`CLI exited: ${output}`)); });
    child.stderr.on('data', chunk => { output += chunk; });
    child.stdout.on('data', chunk => {
      output += chunk;
      const match = /OTA admin listening on http:\/\/127\.0\.0\.1:(\d+)/.exec(output);
      if (match) { clearTimeout(timeout); done(Number(match[1])); }
    });
  });
  assert.equal((await upload(port)).status, 201);
  child.kill();
  await new Promise(done => child.once('exit', done));
  assert.equal((await (await openReleaseStore({ directory: data, key })).list()).releases.length, 1);
  assert.equal(output.includes(token), false);
  assert.equal(output.includes(key.toString('hex')), false);
});
