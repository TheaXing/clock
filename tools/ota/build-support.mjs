import { createHash, timingSafeEqual } from 'node:crypto';
import { lstat, mkdir, mkdtemp, open, readFile, rename, rm } from 'node:fs/promises';
import { basename, dirname, join, resolve } from 'node:path';
import { BOOT_BASE, APP_BASE, createFactoryHex, createPackage, packageReport, releaseKey, validateVectors } from './protocol.mjs';
import { validatePackagePath } from './server.mjs';
import { validateDiscoveryUrl } from './discovery-config.mjs';

function optionsFor(args, allowed) {
  const options = {};
  for (let index = 0; index < args.length; index += 2) {
    const name = args[index];
    if (!allowed.includes(name) || options[name] !== undefined || args[index + 1] === undefined) {
      throw new Error('Invalid, duplicate or missing build-support argument');
    }
    options[name] = args[index + 1];
  }
  return options;
}

function required(options, name) {
  if (!options[name]) {
    throw new Error(`Required option missing: ${name}`);
  }
  return options[name];
}

function versionNumber(value) {
  if (!/^[0-9]+$/.test(value ?? '')) {
    throw new Error('Firmware version must be a positive u32 integer');
  }
  const version = Number(value);
  if (!Number.isSafeInteger(version) || version < 1 || version > 0xffffffff) {
    throw new Error('Firmware version is outside the positive u32 range');
  }
  return version;
}

function nonzeroHex(value, label) {
  if (typeof value !== 'string' || !/^[0-9a-f]{64}$/i.test(value) || /^0{64}$/.test(value)) {
    throw new Error(`${label} must be a nonzero 32-byte hexadecimal value`);
  }
  return value.toLowerCase();
}

function sameHex(left, right) {
  return timingSafeEqual(Buffer.from(left, 'hex'), Buffer.from(right, 'hex'));
}

async function loadConfiguration(configPath, keyPath) {
  let text;
  try {
    text = await readFile(configPath, 'utf8');
  } catch {
    throw new Error('Unable to read the actual OTA configuration; file missing or inaccessible');
  }
  let keys;
  try {
    keys = JSON.parse(await readFile(keyPath, 'utf8'));
  } catch {
    throw new Error('Unable to read a valid OTA key JSON file');
  }
  const keyHex = nonzeroHex(keys.keyHex, 'Release key');
  const adminTokenHex = nonzeroHex(keys.adminTokenHex, 'Administration token');
  const uncommented = text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\r\n]*/g, '');
  function macro(name, optional = false) {
    const values = [...uncommented.matchAll(new RegExp(`^\\s*#\\s*define\\s+${name}\\s+([^\\r\\n]*)`, 'gm'))];
    if (values.length === 0 && optional) {
      return undefined;
    }
    if (values.length !== 1) {
      throw new Error(`OTA configuration requires one literal ${name} definition`);
    }
    return values[0][1].trim();
  }
  function quoted(name, optional = false) {
    const value = macro(name, optional);
    if (value === undefined) {
      return undefined;
    }
    const match = /^"([^"\\]*)"$/.exec(value);
    if (!match) {
      throw new Error(`OTA configuration requires a literal string for ${name}`);
    }
    return match[1];
  }
  const configKey = nonzeroHex(quoted('CLOCK_OTA_KEY_HEX'), 'Configured release key');
  if (!sameHex(keyHex, configKey)) {
    throw new Error('OTA configuration release key does not match the key file');
  }
  const configToken = quoted('CLOCK_OTA_ADMIN_TOKEN_HEX', true);
  if (configToken !== undefined && configToken !== '' && !sameHex(nonzeroHex(configToken, 'Configured administration token'), adminTokenHex)) {
    throw new Error('OTA configuration administration token does not match the key file');
  }
  const digest = createHash('sha256').update(Buffer.from(adminTokenHex, 'hex')).digest('hex');
  const configDigest = nonzeroHex(quoted('CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX'), 'Configured administration token digest');
  if (!sameHex(digest, configDigest)) {
    throw new Error('OTA configuration administration token digest does not match the key file');
  }
  const host = quoted('CLOCK_OTA_HOST');
  if (!/^[A-Za-z0-9][A-Za-z0-9.-]{0,63}$/.test(host)) {
    throw new Error('Configured OTA host must be an IPv4 address or hostname of at most 64 ASCII bytes');
  }
  const portText = macro('CLOCK_OTA_PORT');
  if (!/^[0-9]+[uU]?$/.test(portText) || Number(portText.replace(/[uU]$/, '')) < 1 || Number(portText.replace(/[uU]$/, '')) > 65535) {
    throw new Error('Configured OTA port must be in the range 1..65535');
  }
  const path = validatePackagePath(quoted('CLOCK_OTA_PATH'));
  if (Buffer.byteLength(path) > 156) {
    throw new Error('Configured OTA path must be at most 156 bytes for automatic target URLs');
  }
  validateDiscoveryUrl(host, Number(portText.replace(/[uU]$/, '')), path);
  const versionText = macro('CLOCK_OTA_VERSION');
  if (!/^[0-9]+[uU]?$/.test(versionText)) {
    throw new Error('Configured OTA version must be a literal integer');
  }
  const autoText = macro('CLOCK_OTA_AUTO_CHECK', true);
  if (autoText !== undefined && !/^[01][uU]?$/.test(autoText)) {
    throw new Error('Configured OTA automatic check must be 0 or 1');
  }
  return { text, keyHex, adminTokenHex, version: Number(versionText.replace(/[uU]$/, '')), hasAutoCheck: autoText !== undefined };
}

async function ensureNewOutput(directory) {
  try {
    await lstat(directory);
  } catch (error) {
    if (error.code === 'ENOENT') {
      return;
    }
    throw new Error('Unable to inspect the output directory');
  }
  throw new Error('Refusing to overwrite an existing output directory');
}

async function writeSynced(path, content) {
  const handle = await open(path, 'wx', 0o600);
  try {
    await handle.writeFile(content);
    await handle.sync();
  } finally {
    await handle.close();
  }
}

async function prepare(options) {
  const version = versionNumber(required(options, '--version'));
  const output = resolve(required(options, '--output-dir'));
  const generated = resolve(required(options, '--generated-config'));
  const config = await loadConfiguration(required(options, '--config'), required(options, '--key-file'));
  await ensureNewOutput(output);
  let text = config.text.replace(/^(\s*#\s*define\s+CLOCK_OTA_VERSION\s+)[^\r\n]*/m, (_line, prefix) => `${prefix}${version}u`);
  if (!config.hasAutoCheck) {
    text += '\n#ifndef CLOCK_OTA_AUTO_CHECK\n#define CLOCK_OTA_AUTO_CHECK 1\n#endif\n';
  }
  await mkdir(dirname(generated), { recursive: true, mode: 0o700 });
  await writeSynced(generated, text);
}

function containsSecret(bytes, hex) {
  return bytes.includes(Buffer.from(hex, 'hex')) || bytes.toString('latin1').toLowerCase().includes(hex.toLowerCase());
}

function validateArtifacts(app, boot, config) {
  validateVectors(app);
  validateVectors(boot, BOOT_BASE, APP_BASE - BOOT_BASE);
  if (containsSecret(app, config.keyHex)) {
    throw new Error('App contains the private release key');
  }
  if (containsSecret(app, config.adminTokenHex)) {
    throw new Error('App contains the raw administration token');
  }
  if (!containsSecret(boot, config.keyHex)) {
    throw new Error('Boot does not contain the configured release key');
  }
  if (containsSecret(boot, config.adminTokenHex)) {
    throw new Error('Boot contains the raw administration token');
  }
}

async function publish(options) {
  const version = versionNumber(required(options, '--version'));
  const output = resolve(required(options, '--output-dir'));
  const config = await loadConfiguration(required(options, '--config'), required(options, '--key-file'));
  if (config.version !== version) {
    throw new Error('Generated configuration version does not match the package version');
  }
  if (!['true', 'false'].includes(required(options, '--factory'))) {
    throw new Error('Factory option must be true or false');
  }
  const [app, boot] = await Promise.all([readFile(required(options, '--app')), readFile(required(options, '--boot'))]);
  validateArtifacts(app, boot, config);
  const key = releaseKey(config.keyHex);
  const bytes = createPackage(app, key, version);
  const report = { ...packageReport(bytes, key), bootSize: boot.length, factoryHex: options['--factory'] === 'true', artifactChecks: 'vectors, Flash capacity and private key isolation passed' };
  const factory = report.factoryHex ? createFactoryHex(boot, bytes, key) : undefined;
  await mkdir(dirname(output), { recursive: true });
  const lock = `${output}.lock`;
  try {
    await mkdir(lock);
  } catch {
    throw new Error('Another build is publishing this output directory; output was preserved');
  }
  let temporary;
  try {
    await ensureNewOutput(output);
    temporary = await mkdtemp(join(dirname(output), `.${basename(output)}-pending-`));
    await writeSynced(join(temporary, `clock-v${version}.ota`), bytes);
    await writeSynced(join(temporary, `clock-v${version}-report.json`), `${JSON.stringify(report, null, 2)}\n`);
    if (factory !== undefined) {
      await writeSynced(join(temporary, `clock-factory-v${version}.hex`), factory);
    }
    await rename(temporary, output);
    temporary = undefined;
  } finally {
    if (temporary !== undefined) {
      await rm(temporary, { recursive: true, force: true });
    }
    await rm(lock, { recursive: true, force: true });
  }
  console.log(JSON.stringify({ packagePath: join(output, `clock-v${version}.ota`), version, packageSize: report.packageSize, packageSha256: report.packageSha256, packageId: report.packageId }, null, 2));
}

try {
  const [command, ...args] = process.argv.slice(2);
  if (command === 'prepare') {
    await prepare(optionsFor(args, ['--config', '--key-file', '--version', '--output-dir', '--generated-config']));
  } else if (command === 'publish') {
    await publish(optionsFor(args, ['--config', '--key-file', '--version', '--output-dir', '--app', '--boot', '--factory']));
  } else {
    throw new Error('Build-support requires prepare or publish');
  }
} catch (error) {
  const expected = ['ENOENT', 'EACCES', 'EPERM', 'EISDIR', 'ENOTDIR', 'ENOSPC'];
  console.error(expected.includes(error.code) ? `OTA build preparation failed (${error.code})` : `OTA build preparation failed: ${error.message}`);
  process.exitCode = 1;
}
