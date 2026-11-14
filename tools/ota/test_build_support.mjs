import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { copyFile, mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { verifyPackage } from './protocol.mjs';

const sourceRoot = resolve(import.meta.dirname, '../..');
const keyHex = '000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f';
const adminTokenHex = 'e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff';
const tokenDigest = createHash('sha256').update(Buffer.from(adminTokenHex, 'hex')).digest('hex');

function configFixture(extra = '') {
  return `#ifndef CLOCK_OTA_CONFIG_LOCAL_H\n#define CLOCK_OTA_CONFIG_LOCAL_H\n#define CLOCK_OTA_HOST "192.168.10.22"\n#define CLOCK_OTA_PORT 8788\n#define CLOCK_OTA_PATH "/firmware.ota"\n#define CLOCK_OTA_KEY_HEX "${keyHex}"\n#define CLOCK_OTA_ADMIN_TOKEN_HEX "${adminTokenHex}"\n#define CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX "${tokenDigest}"\n#define CLOCK_OTA_VERSION 77u\n${extra}\n#endif\n`;
}

function imageFixture(base) {
  const image = Buffer.alloc(0x300, 0xa5);
  image.writeUInt32LE(0x20020000, 0);
  image.writeUInt32LE(base + 0x189, 4);
  return image;
}

function mixedHex(value) {
  let index = 0;
  return value.replace(/[a-f]/g, character => index++ % 2 ? character.toUpperCase() : character);
}

// Replace only the external compiler boundary in an isolated repository copy.
const compilerFixture = `param([string]$Configuration, [switch]$Ota, [string]$OtaConfig, [switch]$ActiveBuzzer)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$record = @{ configuration = $Configuration; ota = [bool]$Ota; config = $OtaConfig; configText = (Get-Content -Raw -LiteralPath $OtaConfig); active = [bool]$ActiveBuzzer; activeExplicit = $PSBoundParameters.ContainsKey('ActiveBuzzer') }
$record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'invocation.json') -Encoding utf8
if ($env:FIXTURE_COMPILE_FAIL -eq '1') { throw 'Controlled compiler failure' }
if ($env:FIXTURE_HOLD_BUILD -eq '1') {
    [System.IO.File]::WriteAllText((Join-Path $root 'compile-entered'), 'ready')
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while (!(Test-Path -LiteralPath (Join-Path $root 'compile-release'))) {
        if ([DateTime]::UtcNow -gt $deadline) { throw 'Controlled build hold timed out' }
        Start-Sleep -Milliseconds 50
    }
}
$target = Join-Path $root "build/Ota$Configuration"
New-Item -ItemType Directory -Path $target -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'app.template.bin') -Destination (Join-Path $target 'Clock_CubeMX.bin')
Copy-Item -LiteralPath (Join-Path $root 'boot.template.bin') -Destination (Join-Path $target 'Clock_Bootloader.bin')
`;

async function fixture(t, { extraConfig = '', application = imageFixture(0x08010000), bootloader } = {}) {
  const root = await mkdtemp(join(tmpdir(), 'clock-ota-build-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  await Promise.all(['scripts', 'tools/ota', 'app', 'build/OtaRelease'].map(path => mkdir(join(root, path), { recursive: true })));
  for (const path of ['scripts/build_ota.ps1', 'scripts/start_ota_server.ps1', 'tools/ota/build-support.mjs', 'tools/ota/protocol.mjs', 'tools/ota/server.mjs', 'tools/ota/discovery-config.mjs']) {
    try {
      await copyFile(join(sourceRoot, path), join(root, path));
    } catch (error) {
      if (error.code !== 'ENOENT') {
        throw error;
      }
    }
  }
  const boot = bootloader ?? imageFixture(0x08000000);
  if (bootloader === undefined) {
    Buffer.from(keyHex, 'ascii').copy(boot, 0x200);
  }
  await Promise.all([
    writeFile(join(root, 'scripts/build.ps1'), compilerFixture),
    writeFile(join(root, 'app/ota_config_local.h'), configFixture(extraConfig)),
    writeFile(join(root, 'app/ota_key_local.json'), JSON.stringify({ formatVersion: 1, keyHex, adminTokenHex })),
    writeFile(join(root, 'app.template.bin'), application),
    writeFile(join(root, 'boot.template.bin'), boot),
    writeFile(join(root, 'build/OtaRelease/CMakeCache.txt'), 'CLOCK_OTA_CONFIG_FILE:FILEPATH=build/ota-validation/ota_config_local.h\n')
  ]);
  return root;
}

function runScript(root, script, args = [], env = {}) {
  return spawnSync('pwsh', ['-NoLogo', '-NoProfile', '-NonInteractive', '-File', join(root, 'scripts', script), ...args], {
    cwd: root,
    encoding: 'utf8',
    timeout: 20000,
    env: { ...process.env, ...env }
  });
}

function runBuild(root, args = [], env = {}) {
  return runScript(root, 'build_ota.ps1', ['-Version', '9', '-Configuration', 'Release', ...args], env);
}

function assertNoSecrets(result) {
  for (const secret of [keyHex, keyHex.toUpperCase(), adminTokenHex, adminTokenHex.toUpperCase()]) {
    assert.equal(`${result.stdout}${result.stderr}`.includes(secret), false, 'Private values must never appear in terminal output');
  }
}

async function assertNotCreated(path) {
  await assert.rejects(readFile(path), { code: 'ENOENT' });
}

test('missing actual configuration fails before invoking compiler or creating outputs', async t => {
  const root = await fixture(t);
  await rm(join(root, 'app/ota_config_local.h'));
  const result = runBuild(root);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /configuration.*(missing|not found)|unable to read.*configuration/i);
  await assertNotCreated(join(root, 'invocation.json'));
  await assertNotCreated(join(root, 'build/ota-packages/v9/clock-v9.ota'));
  assertNoSecrets(result);
});

test('concurrent versions cannot replace shared binaries and the process lock releases after completion', async t => {
  const root = await fixture(t);
  const child = spawn('pwsh', ['-NoLogo', '-NoProfile', '-NonInteractive', '-File', join(root, 'scripts/build_ota.ps1'), '-Version', '9'], {
    cwd: root, env: { ...process.env, FIXTURE_HOLD_BUILD: '1' }
  });
  t.after(() => { if (child.exitCode === null) { child.kill(); } });
  let stdout = '';
  let stderr = '';
  child.stdout.on('data', bytes => { stdout += bytes; });
  child.stderr.on('data', bytes => { stderr += bytes; });
  const completed = new Promise((resolveExit, reject) => {
    child.on('error', reject);
    child.on('close', status => resolveExit({ status, stdout, stderr }));
  });
  let entered = false;
  for (let attempt = 0; attempt < 200; ++attempt) {
    entered = await readFile(join(root, 'compile-entered')).then(() => true, () => false);
    if (entered) { break; }
    await new Promise(resolveWait => setTimeout(resolveWait, 25));
  }
  assert.equal(entered, true, 'The first process must hold the actual shared compiler boundary');
  const second = runScript(root, 'build_ota.ps1', ['-Version', '10']);
  await writeFile(join(root, 'compile-release'), 'release');
  const first = await completed;
  assert.equal(first.status, 0, first.stderr);
  assert.notEqual(second.status, 0, 'The second version must fail before touching shared build artifacts');
  assert.match(second.stderr, /another.*OTA.*build|OTA.*build.*running/i);
  assert.match(JSON.parse(await readFile(join(root, 'invocation.json'), 'utf8')).configText, /^#define CLOCK_OTA_VERSION 9u$/m);
  assert.equal(verifyPackage(await readFile(join(root, 'build/ota-packages/v9/clock-v9.ota')), keyHex).version, 9);
  await assertNotCreated(join(root, 'build/ota-packages/v10/clock-v10.ota'));
  const later = runScript(root, 'build_ota.ps1', ['-Version', '10']);
  assert.equal(later.status, 0, later.stderr);
  assert.equal(verifyPackage(await readFile(join(root, 'build/ota-packages/v10/clock-v10.ota')), keyHex).version, 10);
  assertNoSecrets(first);
  assertNoSecrets(second);
});

test('a release key mismatch is rejected before compiling without revealing either key', async t => {
  const root = await fixture(t);
  await writeFile(join(root, 'app/ota_key_local.json'), JSON.stringify({ formatVersion: 1, keyHex: 'ab'.repeat(32), adminTokenHex }));
  const result = runBuild(root);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /release key.*(match|differ)|key.*mismatch/i);
  await assertNotCreated(join(root, 'invocation.json'));
  assertNoSecrets(result);
  assert.equal(result.stderr.includes('ab'.repeat(32)), false);
});

test('versions outside positive u32 range are rejected before compiling', async t => {
  const root = await fixture(t);
  for (const version of ['0', '4294967296', '1.5', '-1']) {
    const result = runScript(root, 'build_ota.ps1', ['-Version', version]);
    assert.notEqual(result.status, 0, version);
    assert.match(result.stderr, /version.*(positive|u32|range|integer)/i, version);
    await assertNotCreated(join(root, 'invocation.json'));
  }
});

test('explicit generated config overrides stale CMake state, synchronizes version and enables automatic checks', async t => {
  const root = await fixture(t);
  const original = await readFile(join(root, 'app/ota_config_local.h'), 'utf8');
  const result = runBuild(root, ['-Factory']);
  assert.equal(result.status, 0, result.stderr);
  const invocation = JSON.parse(await readFile(join(root, 'invocation.json'), 'utf8'));
  assert.equal(invocation.ota, true);
  assert.equal(invocation.configuration, 'Release');
  assert.equal(invocation.activeExplicit, true);
  assert.equal(invocation.active, false);
  assert.ok(invocation.config.startsWith(join(root, 'build/ota-private')));
  assert.match(invocation.configText, /^#define CLOCK_OTA_VERSION 9u$/m);
  assert.match(invocation.configText, /^#define CLOCK_OTA_AUTO_CHECK 1$/m);
  assert.equal(await readFile(join(root, 'app/ota_config_local.h'), 'utf8'), original);
  const packageBytes = await readFile(join(root, 'build/ota-packages/v9/clock-v9.ota'));
  assert.equal(verifyPackage(packageBytes, keyHex).version, 9);
  const report = JSON.parse(await readFile(join(root, 'build/ota-packages/v9/clock-v9-report.json'), 'utf8'));
  assert.equal(report.version, 9);
  assert.equal(report.packageSize, 1024);
  assert.equal(report.factoryHex, true);
  assert.ok((await readFile(join(root, 'build/ota-packages/v9/clock-factory-v9.hex'), 'utf8')).endsWith(':00000001FF\n'));
  await assertNotCreated(invocation.config);
  assert.deepEqual((await readdir(join(root, 'build/ota-packages'))).sort(), ['v9']);
  assertNoSecrets(result);
});

test('maximum u32 version, explicit paths and active buzzer reach the compiled and packaged build', async t => {
  const root = await fixture(t, { extraConfig: '#define CLOCK_OTA_AUTO_CHECK 0' });
  const customConfig = join(root, 'app/custom configuration.h');
  const customKey = join(root, 'app/custom keys.json');
  const output = join(root, 'custom packages/v4294967295');
  await copyFile(join(root, 'app/ota_config_local.h'), customConfig);
  await copyFile(join(root, 'app/ota_key_local.json'), customKey);
  const result = runScript(root, 'build_ota.ps1', ['-Version', '4294967295', '-Configuration', 'Debug', '-OtaConfig', customConfig, '-KeyFile', customKey, '-OutputDirectory', output, '-ActiveBuzzer']);
  assert.equal(result.status, 0, result.stderr);
  const invocation = JSON.parse(await readFile(join(root, 'invocation.json'), 'utf8'));
  assert.equal(invocation.configuration, 'Debug');
  assert.equal(invocation.activeExplicit, true);
  assert.equal(invocation.active, true);
  assert.match(invocation.configText, /^#define CLOCK_OTA_VERSION 4294967295u$/m);
  assert.match(invocation.configText, /^#define CLOCK_OTA_AUTO_CHECK 0$/m);
  assert.equal(verifyPackage(await readFile(join(output, 'clock-v4294967295.ota')), keyHex).version, 4294967295);
  assert.deepEqual((await readdir(output)).sort(), ['clock-v4294967295-report.json', 'clock-v4294967295.ota']);
  assertNoSecrets(result);
});

test('an existing output directory is preserved and refused before compiling', async t => {
  const root = await fixture(t);
  const output = join(root, 'build/ota-packages/v9');
  await mkdir(output, { recursive: true });
  await writeFile(join(output, 'keep.txt'), 'previous package');
  const result = runBuild(root);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /overwrite|already exists|existing output/i);
  assert.equal(await readFile(join(output, 'keep.txt'), 'utf8'), 'previous package');
  await assertNotCreated(join(root, 'invocation.json'));
});

test('compiler failure cannot leave a publishable package or private config behind', async t => {
  const root = await fixture(t);
  const result = runBuild(root, [], { FIXTURE_COMPILE_FAIL: '1' });
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /controlled compiler failure/i);
  const invocation = JSON.parse(await readFile(join(root, 'invocation.json'), 'utf8'));
  await assertNotCreated(join(root, 'build/ota-packages/v9/clock-v9.ota'));
  await assertNotCreated(invocation.config);
  assertNoSecrets(result);
});

test('compiled App containing a private release key fails before output publication', async t => {
  const application = imageFixture(0x08010000);
  Buffer.from(keyHex, 'hex').copy(application, 0x200);
  const root = await fixture(t, { application });
  const result = runBuild(root);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /app.*(release key|secret)/i);
  await assertNotCreated(join(root, 'build/ota-packages/v9/clock-v9.ota'));
  assertNoSecrets(result);
});

test('Boot built with another key cannot produce a publishable package', async t => {
  const root = await fixture(t, { bootloader: imageFixture(0x08000000) });
  const result = runBuild(root);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /boot.*(release key|configured key)/i);
  await assertNotCreated(join(root, 'build/ota-packages/v9/clock-v9.ota'));
  assertNoSecrets(result);
});

test('mixed-case hexadecimal secrets in an App are detected before publication', async t => {
  for (const secret of [keyHex, adminTokenHex]) {
    const application = imageFixture(0x08010000);
    const mixedCase = mixedHex(secret);
    assert.notEqual(mixedCase, secret);
    assert.notEqual(mixedCase, secret.toUpperCase());
    Buffer.from(mixedCase, 'ascii').copy(application, 0x200);
    const root = await fixture(t, { application });
    const result = runBuild(root);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /app.*(release key|administration token)/i);
    await assertNotCreated(join(root, 'build/ota-packages/v9/clock-v9.ota'));
    assertNoSecrets(result);
    assert.equal(`${result.stdout}${result.stderr}`.includes(mixedCase), false);
  }
});

test('a Boot with an equivalent mixed-case hexadecimal release key is accepted', async t => {
  const bootloader = imageFixture(0x08000000);
  Buffer.from(mixedHex(keyHex), 'ascii').copy(bootloader, 0x200);
  const root = await fixture(t, { bootloader });
  const result = runBuild(root);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(verifyPackage(await readFile(join(root, 'build/ota-packages/v9/clock-v9.ota')), keyHex).version, 9);
});

test('an output I/O failure exposes no partial package and preserves the blocking file', async t => {
  const root = await fixture(t);
  const parent = join(root, 'blocked');
  await writeFile(parent, 'preserve this file');
  const result = runBuild(root, ['-OutputDirectory', join(parent, 'v9')]);
  assert.notEqual(result.status, 0);
  assert.equal(await readFile(parent, 'utf8'), 'preserve this file');
  assertNoSecrets(result);
});

test('failure after package write keeps the entire version directory invisible and removes temporary artifacts', async t => {
  const root = await fixture(t);
  const output = join(root, 'build/ota-packages/v9');
  const marker = join(root, 'write-failure-observation.json');
  const preload = join(root, 'fail-report-write.mjs');
  await writeFile(preload, `import fs from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { syncBuiltinESMExports } from 'node:module';
const originalOpen = fs.open;
fs.open = async function(path, ...args) {
  if (String(path).endsWith('clock-v9-report.json')) {
    const size = (await fs.readFile(join(dirname(path), 'clock-v9.ota'))).length;
    const outputVisible = await fs.lstat(${JSON.stringify(output)}).then(() => true, () => false);
    await fs.writeFile(${JSON.stringify(marker)}, JSON.stringify({ size, outputVisible }));
    throw Object.assign(new Error('Controlled disk full'), { code: 'ENOSPC' });
  }
  return originalOpen(path, ...args);
};
syncBuiltinESMExports();
`);
  const result = runBuild(root, ['-Factory'], { NODE_OPTIONS: `${process.env.NODE_OPTIONS ?? ''} --import=${pathToFileURL(preload).href}` });
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /ENOSPC/);
  assert.deepEqual(JSON.parse(await readFile(marker, 'utf8')), { size: 1024, outputVisible: false });
  assert.deepEqual(await readdir(join(root, 'build/ota-packages')), []);
  assertNoSecrets(result);
});

test('configuration path and admin token inconsistencies are rejected without invoking compiler', async t => {
  const root = await fixture(t);
  for (const [text, error] of [[configFixture().replace('/firmware.ota', `/${'a'.repeat(156)}`), /configured OTA path.*156/i], [configFixture().replace(tokenDigest, '00'.repeat(32)), /administration token.*digest/i]]) {
    await writeFile(join(root, 'app/ota_config_local.h'), text);
    const result = runBuild(root);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, error);
    await assertNotCreated(join(root, 'invocation.json'));
    assertNoSecrets(result);
  }
});

test('startup wrapper uses explicit admin CLI paths and defaults without creating or printing secrets', async t => {
  const root = await fixture(t);
  await writeFile(join(root, 'tools/ota/ota.mjs'), `import { writeFile } from 'node:fs/promises';\nawait writeFile(new URL('../../startup-args.json', import.meta.url), JSON.stringify(process.argv.slice(2)));\nconsole.log('Fixture admin ready');\n`);
  const result = runScript(root, 'start_ota_server.ps1');
  assert.equal(result.status, 0, result.stderr);
  assert.deepEqual(JSON.parse(await readFile(join(root, 'startup-args.json'), 'utf8')), ['admin', '--key-file', join(root, 'app/ota_key_local.json'), '--data-dir', join(root, '.ota-data'), '--host', '0.0.0.0', '--port', '8788', '--path', '/firmware.ota']);
  assert.equal(await readFile(join(root, 'app/ota_key_local.json'), 'utf8'), JSON.stringify({ formatVersion: 1, keyHex, adminTokenHex }));
  assertNoSecrets(result);
});

test('startup wrapper forwards custom address and persistent directory, and rejects a build-directory store', async t => {
  const root = await fixture(t);
  await writeFile(join(root, 'tools/ota/ota.mjs'), `import { writeFile } from 'node:fs/promises';\nawait writeFile(new URL('../../startup-args.json', import.meta.url), JSON.stringify(process.argv.slice(2)));\n`);
  const keyFile = join(root, 'app/ota_key_local.json');
  const directory = join(root, 'persistent releases');
  const result = runScript(root, 'start_ota_server.ps1', ['-KeyFile', keyFile, '-DataDirectory', directory, '-BindAddress', '127.0.0.1', '-Port', '9876', '-PackagePath', '/clock.ota']);
  assert.equal(result.status, 0, result.stderr);
  assert.deepEqual(JSON.parse(await readFile(join(root, 'startup-args.json'), 'utf8')), ['admin', '--key-file', keyFile, '--data-dir', directory, '--host', '127.0.0.1', '--port', '9876', '--path', '/clock.ota']);
  const invalid = runScript(root, 'start_ota_server.ps1', ['-DataDirectory', join(root, 'build/releases')]);
  assert.notEqual(invalid.status, 0);
  assert.match(invalid.stderr, /data.*build|persistent.*build/i);
});
