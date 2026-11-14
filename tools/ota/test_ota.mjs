import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash, createHmac } from 'node:crypto';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { request } from 'node:http';
import { createPackage, parseHeader, verifyPackage, createFactoryHex } from './protocol.mjs';
import { createOtaServer } from './server.mjs';

const key = Buffer.from('000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f', 'hex');
const cli = resolve('tools/ota/ota.mjs');

// Independent binary fixture; all addresses and sizes are literals from the design.
function imageFixture(size = 0x200, base = 0x08010000) {
  const image = Buffer.alloc(size, 0xa5);
  image.writeUInt32LE(0x20020000, 0);
  image.writeUInt32LE(base + 0x189, 4);
  return image;
}

function independentPackage(image = imageFixture(), version = 7) {
  const header = Buffer.alloc(256);
  header.write('CLKOTA1\0', 0, 'ascii');
  [1, 256, 0x08010000, image.length, version, 0].forEach((value, index) => header.writeUInt32LE(value, 8 + index * 4));
  header.write('Clock_CubeMX-STM32F407VE', 32, 'ascii');
  createHash('sha256').update(image).digest().copy(header, 64);
  createHash('sha256').update(header.subarray(0, 96)).digest().copy(header, 96, 0, 16);
  createHmac('sha256', key).update(header.subarray(0, 192)).digest().copy(header, 192);
  header.writeUInt32LE(0xffffffff, 252);
  return Buffer.concat([header, image]);
}

function signHeader(header) {
  createHash('sha256').update(header.subarray(0, 96)).digest().copy(header, 96, 0, 16);
  createHmac('sha256', key).update(header.subarray(0, 192)).digest().copy(header, 192);
}

test('package matches independent exact-format fixture and parses authenticated fields', () => {
  const expected = independentPackage();
  assert.deepEqual(createPackage(imageFixture(), key, 7), expected);
  const parsed = verifyPackage(expected, key);
  assert.equal(parsed.version, 7);
  assert.equal(parsed.imageSize, 512);
  assert.deepEqual(parsed.image, imageFixture());
  assert.deepEqual(parsed.id, expected.subarray(96, 112));
  assert.equal(parseHeader(expected.subarray(0, 256), key).imageSize, 512);
});

test('every authenticated header byte and body mutation is rejected', () => {
  const original = independentPackage();
  for (let index = 0; index < original.length; index++) {
    const mutated = Buffer.from(original);
    mutated[index] ^= 1;
    assert.throws(() => verifyPackage(mutated, key), undefined, `byte ${index}`);
  }
});

test('semantically invalid fields are rejected even after valid reauthentication', () => {
  for (const [offset, value] of [[8, 2], [12, 255], [16, 0x08000000], [20, 0x187], [20, 458497], [28, 1], [112, 1], [224, 1], [252, 0x3141544f]]) {
    const bytes = independentPackage();
    bytes.writeUInt32LE(value, offset);
    signHeader(bytes.subarray(0, 256));
    assert.throws(() => verifyPackage(bytes, key), undefined, `offset ${offset}`);
  }
  const wrongBoard = independentPackage();
  wrongBoard[55] = 1;
  signHeader(wrongBoard.subarray(0, 256));
  assert.throws(() => verifyPackage(wrongBoard, key));
  const footer = independentPackage().subarray(0, 256);
  footer.writeUInt32LE(0x3141544f, 252);
  assert.equal(parseHeader(footer, key, { installed: true }).version, 7);
  assert.throws(() => parseHeader(footer, key));
});

test('exact size and positive u32 versions are enforced', () => {
  for (const size of [0x187, 458497]) {
    assert.throws(() => createPackage(imageFixture(size), key, 1));
  }
  assert.equal(createPackage(imageFixture(458496), key, 0xffffffff).length, 458752);
  for (const version of [-1, 0, 1.5, 0x100000000, NaN, '1']) {
    assert.throws(() => createPackage(imageFixture(), key, version));
  }
  assert.throws(() => verifyPackage(independentPackage().subarray(0, 767), key));
  assert.throws(() => verifyPackage(Buffer.concat([independentPackage(), Buffer.from([0])]), key));
});

test('legacy, malformed stack and out-of-image reset vectors are rejected', () => {
  for (const [stack, reset] of [[0x20000000, 0x08010189], [0x20020008, 0x08010189], [0x2001fffc, 0x08010189], [0x10010000, 0x08010189], [0x20020000, 0x08010188], [0x20020000, 0x08010187], [0x20020000, 0x08010201], [0x20020000, 0x08000189]]) {
    const image = imageFixture();
    image.writeUInt32LE(stack, 0);
    image.writeUInt32LE(reset, 4);
    assert.throws(() => createPackage(image, key, 1), undefined, `${stack.toString(16)}:${reset.toString(16)}`);
    assert.throws(() => verifyPackage(independentPackage(image), key));
  }
  const minimum = imageFixture(0x189);
  assert.equal(verifyPackage(createPackage(minimum, key, 1), key).imageSize, 0x189);
});

test('missing, malformed and all-zero release keys fail closed', () => {
  for (const bad of [undefined, '', '00'.repeat(32), 'gg'.repeat(32), Buffer.alloc(32), Buffer.alloc(31), Buffer.alloc(33)]) {
    assert.throws(() => createPackage(imageFixture(), bad, 1));
    assert.throws(() => verifyPackage(independentPackage(), bad));
  }
  assert.deepEqual(createPackage(imageFixture(), key.toString('hex'), 7), independentPackage());
  assert.throws(() => verifyPackage(independentPackage(), Buffer.alloc(32, 1)));
});

function decodeHex(hex) {
  let upper = 0;
  let eof = false;
  const memory = new Map();
  for (const line of hex.trim().split(/\r?\n/)) {
    assert.match(line, /^:[0-9A-F]+$/);
    const record = Buffer.from(line.slice(1), 'hex');
    assert.equal(record.length, record[0] + 5);
    assert.equal([...record].reduce((sum, byte) => sum + byte, 0) & 0xff, 0);
    assert.equal(eof, false);
    if (record[3] === 4) {
      upper = record.readUInt16BE(4) * 65536;
    } else if (record[3] === 0) {
      for (let index = 0; index < record[0]; index++) {
        const address = upper + record.readUInt16BE(1) + index;
        assert.equal(memory.has(address), false);
        memory.set(address, record[4 + index]);
      }
    } else {
      assert.equal(record[3], 1);
      eof = true;
    }
  }
  assert.equal(eof, true);
  return memory;
}

test('factory Intel HEX installs Boot, relocated App and committed authenticated footer at exact addresses', () => {
  const boot = imageFixture(512, 0x08000000);
  const bytes = independentPackage();
  const memory = decodeHex(createFactoryHex(boot, bytes, key));
  const actual = (address, length) => Buffer.from(Array.from({ length }, (_, index) => memory.get(address + index)));
  assert.deepEqual(actual(0x08000000, boot.length), boot);
  assert.deepEqual(actual(0x08010000, 512), imageFixture());
  const footer = Buffer.from(bytes.subarray(0, 256));
  footer.writeUInt32LE(0x3141544f, 252);
  assert.deepEqual(actual(0x0807ff00, 256), footer);
  assert.equal(memory.size, 1280);
  assert.throws(() => createFactoryHex(imageFixture(65537, 0x08000000), bytes, key));
  assert.throws(() => createFactoryHex(imageFixture(), bytes, key));
  assert.throws(() => createFactoryHex(Buffer.alloc(0), bytes, key));
});

async function startServer(t, bytes = independentPackage()) {
  const server = createOtaServer({ packageBytes: bytes, key, path: '/firmware.ota' });
  await new Promise((resolveListening, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolveListening);
  });
  t.after(() => new Promise(resolveClose => server.close(resolveClose)));
  return { server, port: server.address().port };
}

function httpRequest(port, path = '/firmware.ota', headers = {}, method = 'GET') {
  return new Promise((resolveResponse, reject) => {
    const req = request({ host: '127.0.0.1', port, path, method, headers }, response => {
      const chunks = [];
      response.on('data', chunk => chunks.push(chunk));
      response.on('end', () => resolveResponse({ status: response.statusCode, headers: response.headers, body: Buffer.concat(chunks) }));
    });
    req.on('error', reject);
    req.end();
  });
}

test('server provides strong full-file ETag, exact 206 ranges and HEAD lengths', async t => {
  const { port } = await startServer(t);
  const bytes = independentPackage();
  const etag = `"${createHash('sha256').update(bytes).digest('hex')}"`;
  const header = await httpRequest(port, undefined, { Range: 'bytes=0-255', 'Accept-Encoding': 'identity' });
  assert.equal(header.status, 206);
  assert.equal(header.headers.etag, etag);
  assert.equal(header.headers['content-range'], 'bytes 0-255/768');
  assert.equal(header.headers['content-length'], '256');
  assert.equal(header.headers['content-encoding'], undefined);
  assert.deepEqual(header.body, bytes.subarray(0, 256));
  const tail = await httpRequest(port, undefined, { Range: 'bytes=256-767', 'If-Match': etag });
  assert.deepEqual(tail.body, imageFixture());
  assert.equal(tail.status, 206);
  const head = await httpRequest(port, undefined, {}, 'HEAD');
  assert.equal(head.status, 200);
  assert.equal(head.headers['content-length'], '768');
  assert.equal(head.body.length, 0);
  const headRange = await httpRequest(port, undefined, { Range: 'bytes=0-255' }, 'HEAD');
  assert.equal(headRange.status, 206);
  assert.equal(headRange.headers['content-length'], '256');
  assert.equal(headRange.body.length, 0);
  assert.deepEqual((await httpRequest(port)).body, bytes);
});

test('server rejects malformed, multiple and unsatisfiable ranges with exact total', async t => {
  const { port } = await startServer(t);
  for (const range of ['bytes=768-800', 'bytes=100-99', 'bytes=0-0,2-2', 'items=0-255', 'bytes=-0', 'bytes=9007199254740992-', 'bytes=a-b', 'bytes=--', 'bytes=0-999999999999999999999']) {
    const response = await httpRequest(port, undefined, { Range: range });
    assert.equal(response.status, 416, range);
    assert.equal(response.headers['content-range'], 'bytes */768');
  }
  assert.deepEqual((await httpRequest(port, undefined, { Range: 'bytes=-8' })).body, independentPackage().subarray(-8));
  assert.deepEqual((await httpRequest(port, undefined, { Range: 'bytes=760-' })).body, independentPackage().subarray(-8));
  assert.equal((await httpRequest(port, undefined, { Range: 'bytes=760-999' })).headers['content-range'], 'bytes 760-767/768');
});

test('server exact If-Match is required when supplied and exposes only fixed readonly path', async t => {
  const { port } = await startServer(t);
  for (const value of ['"other"', 'W/"other"', '*', '"a", "b"']) {
    assert.equal((await httpRequest(port, undefined, { Range: 'bytes=0-255', 'If-Match': value })).status, 412);
  }
  for (const path of ['/secret.json', '/../firmware.ota', '/%2e%2e/firmware.ota', '/firmware.ota?key=1', '//firmware.ota', '/firmware.ota/']) {
    assert.equal((await httpRequest(port, path)).status, 404, path);
  }
  assert.equal((await httpRequest(port, undefined, {}, 'POST')).status, 405);
});

test('server authenticates on startup and freezes a private memory snapshot', async t => {
  const bytes = independentPackage();
  const original = Buffer.from(bytes);
  const { port } = await startServer(t, bytes);
  bytes.fill(0);
  assert.deepEqual((await httpRequest(port)).body, original);
  assert.throws(() => createOtaServer({ packageBytes: bytes, key }));
  for (const path of ['/../file', '/%2e/file', '/file?x', '/file#x', '/file\\x']) {
    assert.throws(() => createOtaServer({ packageBytes: original, key, path }));
  }
});

function runCli(args) {
  return spawnSync(process.execPath, [cli, ...args], { encoding: 'utf8' });
}

async function tempDirectory(t) {
  const directory = await mkdtemp(join(tmpdir(), 'clock-ota-test-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  return directory;
}

test('init emits the decoded admin-token verifier and refuses overwriting either existing secret', async t => {
  const directory = await tempDirectory(t);
  const config = join(directory, 'ota_config_local.h');
  const keyFile = join(directory, 'ota-key.json');
  const result = runCli(['init', '--config', config, '--key-file', keyFile]);
  assert.equal(result.status, 0, result.stderr);
  const secrets = JSON.parse(await readFile(keyFile, 'utf8'));
  assert.match(secrets.keyHex, /^[0-9a-f]{64}$/);
  assert.match(secrets.adminTokenHex, /^[0-9a-f]{64}$/);
  assert.notEqual(secrets.keyHex, secrets.adminTokenHex);
  assert.notEqual(secrets.keyHex, '00'.repeat(32));
  const header = await readFile(config, 'utf8');
  assert.match(header, /#define CLOCK_OTA_HOST "192\.168\.1\.10"/);
  assert.match(header, /#define CLOCK_OTA_PORT 8788/);
  assert.match(header, /#define CLOCK_OTA_PATH "\/firmware\.ota"/);
  assert.match(header, /#define CLOCK_OTA_VERSION 1u/);
  assert.ok(header.includes(secrets.keyHex));
  assert.ok(header.includes(secrets.adminTokenHex));
  const verifier = createHash('sha256').update(Buffer.from(secrets.adminTokenHex, 'hex')).digest('hex');
  assert.ok(header.includes(`#define CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX "${verifier}"`));
  assert.equal(result.stdout.includes(secrets.keyHex), false);
  assert.equal(`${result.stdout}${result.stderr}`.includes(secrets.adminTokenHex), false);
  assert.notEqual(runCli(['init', '--config', config, '--key-file', join(directory, 'new.json')]).status, 0);
  assert.equal(await readFile(config, 'utf8'), header);
  await assert.rejects(readFile(join(directory, 'new.json')));
  assert.notEqual(runCli(['init', '--config', join(directory, 'new.h'), '--key-file', keyFile]).status, 0);
  await assert.rejects(readFile(join(directory, 'new.h')));
});

test('CLI packs real files, reports public metadata and produces exact factory HEX', async t => {
  const directory = await tempDirectory(t);
  const input = join(directory, 'app.bin');
  const boot = join(directory, 'boot.bin');
  const keyFile = join(directory, 'key.json');
  const output = join(directory, 'app.ota');
  const factory = join(directory, 'factory.hex');
  const report = join(directory, 'report.json');
  await writeFile(input, imageFixture());
  await writeFile(boot, imageFixture(512, 0x08000000));
  await writeFile(keyFile, JSON.stringify({ keyHex: key.toString('hex') }));
  const result = runCli(['pack', '--app', input, '--key-file', keyFile, '--version', '7', '--output', output, '--boot', boot, '--factory-output', factory, '--report', report]);
  assert.equal(result.status, 0, result.stderr);
  assert.deepEqual(await readFile(output), independentPackage());
  assert.equal(decodeHex(await readFile(factory, 'utf8')).size, 1280);
  const metadata = JSON.parse(await readFile(report, 'utf8'));
  assert.equal(metadata.version, 7);
  assert.equal(metadata.imageSize, 512);
  assert.equal(metadata.packageSize, 768);
  assert.equal(metadata.appAddress, '0x08010000');
  assert.equal(metadata.imageSha256, createHash('sha256').update(imageFixture()).digest('hex'));
  assert.equal(JSON.stringify(metadata).includes(key.toString('hex')), false);
  assert.equal(result.stdout.includes(key.toString('hex')), false);
});

test('CLI invalid arguments, legacy image and bad key fail without leaking secrets or creating package', async t => {
  const directory = await tempDirectory(t);
  const app = join(directory, 'app.bin');
  const keyFile = join(directory, 'key.json');
  const output = join(directory, 'app.ota');
  const secret = key.toString('hex');
  await writeFile(app, imageFixture(512, 0x08000000));
  await writeFile(keyFile, JSON.stringify({ keyHex: secret }));
  for (const args of [[], ['wat'], ['pack'], ['init'], ['init', '--config', join(directory, 'x.h'), '--port', '0'], ['init', '--config', join(directory, 'x.h'), '--path', '/../secret'], ['pack', '--app', app, '--key-file', keyFile, '--version', '7', '--output', output], ['pack', '--unknown', secret]]) {
    const result = runCli(args);
    assert.notEqual(result.status, 0, args.join(' '));
    assert.equal(`${result.stdout}${result.stderr}`.includes(secret), false);
  }
  await assert.rejects(readFile(output));
  await writeFile(app, imageFixture());
  await writeFile(keyFile, JSON.stringify({ keyHex: '00'.repeat(32) }));
  assert.notEqual(runCli(['pack', '--app', app, '--key-file', keyFile, '--version', '7', '--output', output]).status, 0);
  await assert.rejects(readFile(output));
});

test('CLI serve validates its file once and publishes unchanged ranges after disk replacement', async t => {
  const directory = await tempDirectory(t);
  const input = join(directory, 'app.ota');
  const keyFile = join(directory, 'key.json');
  const bytes = independentPackage();
  await writeFile(input, bytes);
  await writeFile(keyFile, JSON.stringify({ keyHex: key.toString('hex') }));
  const reservation = createOtaServer({ packageBytes: bytes, key });
  await new Promise(resolveListening => reservation.listen(0, '127.0.0.1', resolveListening));
  const port = reservation.address().port;
  await new Promise(resolveClose => reservation.close(resolveClose));
  const child = spawn(process.execPath, [cli, 'serve', '--input', input, '--key-file', keyFile, '--host', '127.0.0.1', '--port', String(port)], { stdio: ['ignore', 'pipe', 'pipe'] });
  t.after(() => new Promise(resolveExit => {
    if (child.exitCode !== null || child.signalCode !== null) {
      resolveExit();
      return;
    }
    child.once('exit', resolveExit);
    child.kill();
  }));
  let output = '';
  await new Promise((resolveStarted, reject) => {
    const timeout = setTimeout(() => reject(new Error('CLI server did not start')), 5000);
    child.once('exit', () => {
      clearTimeout(timeout);
      reject(new Error('CLI server exited before startup'));
    });
    child.stderr.on('data', () => {
      clearTimeout(timeout);
      reject(new Error('CLI server reported an error'));
    });
    child.stdout.on('data', chunk => {
      output += chunk.toString();
      if (output.includes('Serving frozen OTA package')) {
        clearTimeout(timeout);
        resolveStarted();
      }
    });
  });
  await writeFile(input, Buffer.alloc(768));
  const loggedRange = new Promise((resolveLogged, reject) => {
    const timeout = setTimeout(() => reject(new Error('CLI did not log completed public range metadata')), 1000);
    child.stdout.on('data', () => {
      if (output.includes('HTTP 206 bytes 0-255/768')) {
        clearTimeout(timeout);
        resolveLogged();
      }
    });
  });
  const response = await httpRequest(port, undefined, { Range: 'bytes=0-255' });
  await loggedRange;
  assert.equal(response.status, 206);
  assert.deepEqual(response.body, bytes.subarray(0, 256));
  assert.equal(output.includes(key.toString('hex')), false);
  await rm(input);
  assert.deepEqual((await httpRequest(port)).body, bytes);
});

test('init and pack preserve all outputs when any chosen destination already exists', async t => {
  const directory = await tempDirectory(t);
  const keyFile = join(directory, 'key.json');
  const image = join(directory, 'app.bin');
  const output = join(directory, 'app.ota');
  const report = join(directory, 'report.json');
  await writeFile(image, imageFixture());
  await writeFile(keyFile, JSON.stringify({ keyHex: key.toString('hex') }));
  await writeFile(report, 'existing report');
  const result = runCli(['pack', '--app', image, '--key-file', keyFile, '--version', '1', '--output', output, '--report', report]);
  assert.notEqual(result.status, 0);
  assert.equal(await readFile(report, 'utf8'), 'existing report');
  await assert.rejects(readFile(output));
  const config = join(directory, 'same.h');
  assert.notEqual(runCli(['init', '--config', config, '--key-file', config]).status, 0);
  await assert.rejects(readFile(config));
});

test('init rejects a host longer than the ESP 64-byte limit before creating secrets', async t => {
  const directory = await tempDirectory(t);
  const config = join(directory, 'ota_config_local.h');
  const keyFile = join(directory, 'ota_key_local.json');
  const host = `${'a'.repeat(32)}.${'b'.repeat(32)}`;
  const rejected = runCli(['init', '--config', config, '--key-file', keyFile, '--host', host]);
  assert.notEqual(rejected.status, 0);
  await assert.rejects(readFile(config));
  await assert.rejects(readFile(keyFile));
  const accepted = runCli(['init', '--config', config, '--key-file', keyFile, '--host', host.slice(0, 64)]);
  assert.equal(accepted.status, 0, accepted.stderr);
});
