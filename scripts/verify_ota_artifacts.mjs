import { readFile } from 'node:fs/promises';
import assert from 'node:assert/strict';

const [appPath, bootPath, keyPath] = process.argv.slice(2);
if (!appPath || !bootPath || !keyPath || process.argv.length !== 5) {
  throw new Error('Usage: node scripts/verify_ota_artifacts.mjs APP.bin BOOT.bin PRIVATE_KEYS.json');
}
const [app, boot, keyText] = await Promise.all([
  readFile(appPath), readFile(bootPath), readFile(keyPath, 'utf8'),
]);
const keys = JSON.parse(keyText);
for (const name of ['keyHex', 'adminTokenHex']) {
  if (typeof keys[name] !== 'string' || !/^[0-9a-f]{64}$/i.test(keys[name])) {
    throw new Error('The private key file is invalid.');
  }
  const representations = [Buffer.from(keys[name].toLowerCase()),
    Buffer.from(keys[name].toUpperCase()), Buffer.from(keys[name], 'hex')];
  assert.equal(representations.some((secret) => app.includes(secret)), false,
    name === 'keyHex' ? 'App must not contain the release key' : 'App must not contain the raw administration token');
  if (name === 'keyHex') {
    assert.ok(representations.some((secret) => boot.includes(secret)),
      'Boot must contain the configured release key');
  } else {
    assert.equal(representations.some((secret) => boot.includes(secret)), false,
      'Boot does not need the raw administration token');
  }
}
assert.ok(app.length >= 0x188 && app.length <= 458496, 'App length must fit its region');
assert.ok(boot.length >= 0x188 && boot.length <= 65536, 'Boot length must fit its region');
assert.equal(app.readUInt32LE(0), 0x20020000, 'App SRAM stack must retain its expected top');
const entry = app.readUInt32LE(4);
assert.equal(entry & 1, 1, 'App reset entry must be Thumb');
assert.ok(entry - 1 >= 0x08010188 && entry - 1 < 0x08010000 + app.length,
  'App must contain its relocated reset entry');
const bootEntry = boot.readUInt32LE(4);
assert.equal(bootEntry & 1, 1, 'Boot reset entry must be Thumb');
assert.ok(bootEntry - 1 >= 0x08000188 && bootEntry - 1 < 0x08010000,
  'Boot reset entry must be in the Boot region');
console.log(`OTA artifact checks passed: App ${app.length} B; Boot ${boot.length} B; no downloadable release key or raw token`);
