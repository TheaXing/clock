import { createHash, randomBytes } from 'node:crypto';
import { access, readFile, writeFile, unlink } from 'node:fs/promises';
import { resolve, sep } from 'node:path';
import { createPackage, createFactoryHex, packageReport, releaseKey } from './protocol.mjs';
import { createOtaServer, validatePackagePath } from './server.mjs';
import { openReleaseStore } from './release-store.mjs';
import { createAdminServer } from './admin-server.mjs';
import { validateDiscoveryUrl } from './discovery-config.mjs';

const usage = `Usage:
  node tools/ota/ota.mjs init --config FILE [--key-file FILE] [--host HOST] [--port 8788] [--path /firmware.ota] [--version 1]
  node tools/ota/ota.mjs pack --app FILE --key-file FILE --version N --output FILE [--boot FILE --factory-output FILE] [--report FILE]
  node tools/ota/ota.mjs serve --input FILE --key-file FILE [--host 0.0.0.0] [--port 8788] [--path /firmware.ota]
  node tools/ota/ota.mjs admin --key-file FILE [--data-dir .ota-data] [--host 0.0.0.0] [--port 8788] [--path /firmware.ota] [--max-bytes 268435456]`;

function optionsFor(args, allowed) {
  const options = {};
  for (let index = 0; index < args.length; index += 2) {
    const name = args[index];
    if (!allowed.includes(name) || options[name] !== undefined || args[index + 1] === undefined || args[index + 1].startsWith('--')) {
      throw new Error('Invalid, duplicate or missing command argument; use --help');
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

function integer(value, maximum, label) {
  if (!/^[0-9]+$/.test(value ?? '')) {
    throw new Error(`${label} must be a positive integer`);
  }
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < 1 || parsed > maximum) {
    throw new Error(`${label} is outside its allowed range`);
  }
  return parsed;
}

function hostName(value) {
  if (!/^[A-Za-z0-9][A-Za-z0-9.-]{0,63}$/.test(value)) {
    throw new Error('Host must be an IPv4 address or DNS hostname of at most 64 bytes');
  }
  return value;
}

async function existing(path) {
  try {
    await access(path);
    return true;
  } catch (error) {
    if (error.code === 'ENOENT') {
      return false;
    }
    throw new Error('Unable to inspect output file');
  }
}

async function writeNewFiles(files) {
  const paths = files.map(([path]) => resolve(path));
  if (new Set(paths).size !== paths.length) {
    throw new Error('Output files must have distinct paths');
  }
  for (const path of paths) {
    if (await existing(path)) {
      throw new Error('Refusing to overwrite an existing output file');
    }
  }
  const created = [];
  try {
    for (let index = 0; index < files.length; index++) {
      await writeFile(paths[index], files[index][1], { flag: 'wx', mode: 0o600 });
      created.push(paths[index]);
    }
  } catch {
    await Promise.allSettled(created.map(path => unlink(path)));
    throw new Error('Could not create output files; existing files were preserved');
  }
}

async function readKey(path) {
  try {
    const document = JSON.parse(await readFile(path, 'utf8'));
    return releaseKey(document.keyHex);
  } catch {
    throw new Error('Unable to read a valid nonzero release key from key file');
  }
}

async function initialize(args) {
  const options = optionsFor(args, ['--config', '--key-file', '--host', '--port', '--path', '--version']);
  const config = required(options, '--config');
  const host = hostName(options['--host'] ?? '192.168.1.10');
  const port = integer(options['--port'] ?? '8788', 65535, 'Port');
  const path = validatePackagePath(options['--path'] ?? '/firmware.ota');
  if (Buffer.byteLength(path) > 156) {
    throw new Error('Automatic OTA package URL must be at most 156 bytes');
  }
  validateDiscoveryUrl(host, port, path);
  const version = integer(options['--version'] ?? '1', 0xffffffff, 'Firmware version');
  const keyHex = randomBytes(32).toString('hex');
  const adminTokenHex = randomBytes(32).toString('hex');
  const adminTokenSha256Hex = createHash('sha256').update(Buffer.from(adminTokenHex, 'hex')).digest('hex');
  const header = `#ifndef CLOCK_OTA_CONFIG_LOCAL_H\n#define CLOCK_OTA_CONFIG_LOCAL_H\n\n/* 本地升级凭据，禁止提交到版本控制。 */\n#define CLOCK_OTA_HOST "${host}"\n#define CLOCK_OTA_PORT ${port}\n#define CLOCK_OTA_PATH "${path}"\n#define CLOCK_OTA_KEY_HEX "${keyHex}"\n#define CLOCK_OTA_ADMIN_TOKEN_HEX "${adminTokenHex}"\n#define CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX "${adminTokenSha256Hex}"\n#define CLOCK_OTA_VERSION ${version}u\n\n#endif\n`;
  const files = [[config, header]];
  if (options['--key-file']) {
    files.push([options['--key-file'], `${JSON.stringify({ formatVersion: 1, keyHex, adminTokenHex }, null, 2)}\n`]);
  }
  await writeNewFiles(files);
  console.log('Created local OTA configuration and requested key file. Keep both private.');
}

async function pack(args) {
  const options = optionsFor(args, ['--app', '--key-file', '--version', '--output', '--boot', '--factory-output', '--report']);
  const input = required(options, '--app');
  const keyFile = required(options, '--key-file');
  const version = integer(required(options, '--version'), 0xffffffff, 'Firmware version');
  const output = required(options, '--output');
  if (Boolean(options['--boot']) !== Boolean(options['--factory-output'])) {
    throw new Error('--boot and --factory-output must be supplied together');
  }
  const key = await readKey(keyFile);
  const image = await readFile(input);
  const bytes = createPackage(image, key, version);
  const report = packageReport(bytes, key);
  const files = [[output, bytes]];
  if (options['--boot']) {
    const boot = await readFile(options['--boot']);
    files.push([options['--factory-output'], createFactoryHex(boot, bytes, key)]);
    report.bootSize = boot.length;
    report.factoryHex = true;
  }
  if (options['--report']) {
    files.push([options['--report'], `${JSON.stringify(report, null, 2)}\n`]);
  }
  await writeNewFiles(files);
  console.log(JSON.stringify(report, null, 2));
}

async function serve(args) {
  const options = optionsFor(args, ['--input', '--key-file', '--host', '--port', '--path']);
  const input = required(options, '--input');
  const keyFile = required(options, '--key-file');
  const host = hostName(options['--host'] ?? '0.0.0.0');
  const port = integer(options['--port'] ?? '8788', 65535, 'Port');
  const path = validatePackagePath(options['--path'] ?? '/firmware.ota');
  const key = await readKey(keyFile);
  const packageBytes = await readFile(input);
  const server = createOtaServer({ packageBytes, key, path });
  server.on('request', (_req, response) => {
    response.once('finish', () => {
      const range = response.getHeader('Content-Range');
      console.log(`HTTP ${response.statusCode}${range ? ` ${range}` : ''}`);
    });
  });
  await new Promise((resolveListening, reject) => {
    server.once('error', reject);
    server.listen(port, host, resolveListening);
  });
  console.log(`Serving frozen OTA package on http://${host}:${port}${path}`);
  console.log(`Package SHA-256: ${packageReport(packageBytes, key).packageSha256}`);
  for (const signal of ['SIGINT', 'SIGTERM']) {
    process.once(signal, () => server.close());
  }
}

async function admin(args) {
  const options = optionsFor(args, ['--key-file', '--data-dir', '--host', '--port', '--path', '--max-bytes']);
  const keyFile = required(options, '--key-file');
  const host = hostName(options['--host'] ?? '0.0.0.0');
  // Zero selects an ephemeral local port for isolated acceptance testing.
  const port = options['--port'] === '0' ? 0 : integer(options['--port'] ?? '8788', 65535, 'Port');
  const path = validatePackagePath(options['--path'] ?? '/firmware.ota');
  const directory = resolve(options['--data-dir'] ?? '.ota-data');
  const buildDirectory = resolve('build').toLowerCase();
  if (directory.toLowerCase() === buildDirectory || directory.toLowerCase().startsWith(`${buildDirectory}${sep}`)) {
    throw new Error('Persistent OTA data must be outside the disposable build directory');
  }
  const maxBytes = integer(options['--max-bytes'] ?? '268435456', Number.MAX_SAFE_INTEGER, 'Storage quota');
  let key;
  let adminTokenHex;
  try {
    const document = JSON.parse(await readFile(keyFile, 'utf8'));
    key = releaseKey(document.keyHex);
    adminTokenHex = document.adminTokenHex;
  } catch {
    throw new Error('Unable to read valid OTA credentials from the key file');
  }
  const store = await openReleaseStore({ directory, key, maxBytes });
  const server = createAdminServer({ store, adminTokenHex, path });
  await new Promise((resolveListening, reject) => {
    server.once('error', reject);
    server.listen(port, host, resolveListening);
  });
  console.log(`OTA admin listening on http://${host}:${server.address().port}/`);
  console.log(`Device package path: ${path}; persistent data: ${directory}`);
  console.log('Use the management token from your private key file. Upload stages a package; publication is a separate action.');
  for (const signal of ['SIGINT', 'SIGTERM']) {
    process.once(signal, () => server.close());
  }
}

try {
  const [command, ...args] = process.argv.slice(2);
  if (command === '--help' || command === '-h') {
    console.log(usage);
  } else if (command === 'init') {
    await initialize(args);
  } else if (command === 'pack') {
    await pack(args);
  } else if (command === 'serve') {
    await serve(args);
  } else if (command === 'admin') {
    await admin(args);
  } else {
    throw new Error('A valid command is required; use --help');
  }
} catch (error) {
  const expected = ['ENOENT', 'EACCES', 'EPERM', 'EISDIR', 'ENOTDIR', 'EADDRINUSE'];
  console.error(expected.includes(error.code) ? `OTA command failed (${error.code})` : `OTA command failed: ${error.message}`);
  process.exitCode = 1;
}
