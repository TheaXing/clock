import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, writeFile, rm, access } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';

const cli = resolve('tools/ota/ota.mjs');
const support = resolve('tools/ota/build-support.mjs');
const host = 'a'.repeat(64);
const path224 = `/${'b'.repeat(137)}`;
const path225 = `/${'b'.repeat(138)}`;
function run(script, ...args) {
  return spawnSync(process.execPath, [script, ...args], { encoding: 'utf8' });
}

test('init accepts discovery URL224 but rejects URL225 before creating private files', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'clock-discovery-init-'));
  try {
    const accepted = run(cli, 'init', '--config', join(directory, 'good.h'), '--host', host, '--port', '65535', '--path', path224);
    assert.equal(accepted.status, 0, accepted.stderr);
    const rejectedPath = join(directory, 'rejected.h');
    const rejected = run(cli, 'init', '--config', rejectedPath, '--host', host, '--port', '65535', '--path', path225);
    assert.notEqual(rejected.status, 0);
    assert.match(rejected.stderr, /224|HTTPCGET|discovery URL/);
    await assert.rejects(access(rejectedPath));
    const shortHost = run(cli, 'init', '--config', join(directory, 'short-host.h'), '--host', '192.0.2.1', '--path', `/${'c'.repeat(155)}`);
    assert.equal(shortHost.status, 0, shortHost.stderr);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('build preparation validates the combined device discovery URL before writing generated config', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'clock-discovery-build-'));
  try {
    const config = join(directory, 'config.h');
    const key = join(directory, 'key.json');
    assert.equal(run(cli, 'init', '--config', config, '--key-file', key, '--host', host, '--port', '65535', '--path', path224).status, 0);
    const args = ['prepare', '--config', config, '--key-file', key, '--version', '2', '--output-dir', join(directory, 'output')];
    const good = run(support, ...args, '--generated-config', join(directory, 'good-generated.h'));
    assert.equal(good.status, 0, good.stderr);
    await writeFile(config, (await readFile(config, 'utf8')).replace(path224, path225));
    const generated = join(directory, 'bad-generated.h');
    const bad = run(support, ...args, '--generated-config', generated);
    assert.notEqual(bad.status, 0);
    assert.match(bad.stderr, /224|HTTPCGET|discovery URL/);
    await assert.rejects(access(generated));
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
