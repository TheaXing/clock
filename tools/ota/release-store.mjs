import * as defaultFs from 'node:fs/promises';
import { randomBytes } from 'node:crypto';
import { join, resolve } from 'node:path';
import { packageReport, releaseKey, HEADER_SIZE, IMAGE_MAX } from './protocol.mjs';

export const PACKAGE_MAX = HEADER_SIZE + IMAGE_MAX;
const ID = /^[0-9a-f]{32}$/;
const SHA = /^[0-9a-f]{64}$/;

export function storeError(status, message) {
  return Object.assign(new Error(message), { status });
}

function copy(value) {
  return structuredClone(value);
}

// Only a fully written and synchronized file can become visible under its final name.
async function atomicWrite(fs, temporaryDirectory, destination, bytes) {
  const temporary = join(temporaryDirectory, `${randomBytes(16).toString('hex')}.tmp`);
  let handle;
  try {
    handle = await fs.open(temporary, 'wx', 0o600);
    await handle.writeFile(bytes);
    await handle.sync();
    await handle.close();
    handle = undefined;
    await fs.rename(temporary, destination);
  } finally {
    await handle?.close().catch(() => {});
    await fs.unlink(temporary).catch(error => {
      if (error.code !== 'ENOENT') { throw error; }
    });
  }
}

export async function openReleaseStore({ directory, key: keyValue, maxBytes = 256 * 1024 * 1024, fileSystem = defaultFs }) {
  const key = releaseKey(keyValue);
  if (typeof directory !== 'string' || !directory || !Number.isSafeInteger(maxBytes) || maxBytes < 1) {
    throw new Error('A persistent directory and a positive storage quota are required');
  }
  const fs = fileSystem;
  const root = resolve(directory);
  const objects = join(root, 'objects');
  const records = join(root, 'releases');
  const temporaryDirectory = join(root, 'temporary');
  await fs.mkdir(root, { recursive: true, mode: 0o700 });
  await fs.mkdir(objects, { recursive: true, mode: 0o700 });
  await fs.mkdir(records, { recursive: true, mode: 0o700 });
  await fs.mkdir(temporaryDirectory, { recursive: true, mode: 0o700 });
  // An interrupted upload has no visible record and cannot become a release on restart.
  for (const name of await fs.readdir(temporaryDirectory)) {
    if (/^[0-9a-f]{32}\.tmp$/.test(name)) {
      await fs.unlink(join(temporaryDirectory, name));
    }
  }
  const releases = new Map();
  const versions = new Map();
  const objectSizes = new Map();
  let usedBytes = 0;
  let reservedBytes = 0;
  let current = null;
  let queue = Promise.resolve();

  function exclusive(action) {
    const result = queue.then(action);
    queue = result.catch(() => {});
    return result;
  }

  async function readVerified(release) {
    const path = join(objects, `${release.packageSha256}.ota`);
    const info = await fs.lstat(path);
    if (!info.isFile() || info.size !== release.packageSize || info.size > PACKAGE_MAX) {
      throw storeError(503, 'Stored package is missing or inconsistent');
    }
    const bytes = await fs.readFile(path);
    let report;
    try {
      report = packageReport(bytes, key);
    } catch {
      throw storeError(503, 'Stored package failed integrity verification');
    }
    for (const field of ['packageId', 'packageSha256', 'packageSize', 'version', 'imageSize', 'board']) {
      if (report[field] !== release[field]) {
        throw storeError(503, 'Stored package identity is inconsistent');
      }
    }
    return { bytes, release: copy(release) };
  }

  for (const name of await fs.readdir(objects)) {
    if (!/^[0-9a-f]{64}\.ota$/.test(name)) {
      throw new Error('Unexpected object in persistent OTA storage');
    }
    const info = await fs.lstat(join(objects, name));
    if (!info.isFile() || info.size > PACKAGE_MAX) {
      throw new Error('Invalid object in persistent OTA storage');
    }
    objectSizes.set(name.slice(0, -4), info.size);
    usedBytes += info.size;
  }
  for (const name of await fs.readdir(records)) {
    if (!/^[0-9a-f]{32}\.json$/.test(name)) {
      throw new Error('Unexpected release record in persistent OTA storage');
    }
    const recordPath = join(records, name);
    const recordInfo = await fs.lstat(recordPath);
    if (!recordInfo.isFile() || recordInfo.size > 4096) {
      throw new Error('Invalid persistent release record');
    }
    const release = JSON.parse(await fs.readFile(recordPath, 'utf8'));
    if (!ID.test(release.packageId) || name !== `${release.packageId}.json` || !SHA.test(release.packageSha256) ||
        !Number.isFinite(Date.parse(release.uploadedAt)) || versions.has(release.version)) {
      throw new Error('Inconsistent persistent release record');
    }
    await readVerified(release);
    releases.set(release.packageId, release);
    versions.set(release.version, release.packageId);
  }
  try {
    const info = await fs.lstat(join(root, 'current.json'));
    if (!info.isFile() || info.size > 4096) {
      throw new Error('Invalid current release pointer');
    }
    const pointer = JSON.parse(await fs.readFile(join(root, 'current.json'), 'utf8'));
    const release = releases.get(pointer.packageId);
    if (!release || pointer.packageSha256 !== release.packageSha256 || !Number.isFinite(Date.parse(pointer.publishedAt))) {
      throw new Error('Current release pointer does not match a verified release');
    }
    current = { ...release, publishedAt: pointer.publishedAt };
  } catch (error) {
    if (error.code !== 'ENOENT') { throw error; }
  }

  function reserveUpload(size) {
    if (!Number.isSafeInteger(size) || size < 1 || size > PACKAGE_MAX) {
      throw storeError(413, 'Package exceeds the allowed upload size');
    }
    if (usedBytes + reservedBytes + size > maxBytes) {
      throw storeError(507, 'Storage quota reached; existing releases are preserved');
    }
    reservedBytes += size;
    let active = true;
    return {
      size,
      release() {
        if (active) {
          active = false;
          reservedBytes -= size;
        }
      },
      get active() { return active; }
    };
  }

  return {
    reserveUpload,
    async list() {
      return {
        releases: copy([...releases.values()].sort((a, b) => b.version - a.version)),
        current: copy(current),
        storage: { usedBytes, limitBytes: maxBytes }
      };
    },
    async upload(input, { reservation } = {}) {
      if (!Buffer.isBuffer(input) || input.length > PACKAGE_MAX) {
        throw storeError(413, 'Package exceeds the allowed upload size');
      }
      const bytes = Buffer.from(input);
      let report;
      try {
        report = packageReport(bytes, key);
      } catch {
        throw storeError(422, 'Package authentication, board, vectors or digest is invalid');
      }
      return exclusive(async () => {
        const existing = releases.get(report.packageId);
        if (existing) {
          if (existing.packageSha256 !== report.packageSha256) {
            throw storeError(409, 'Package identity conflicts with an existing object');
          }
          await readVerified(existing);
          return { release: copy(existing), created: false };
        }
        if (versions.has(report.version)) {
          throw storeError(409, 'This version already identifies a different package; increment the version');
        }
        const held = reservation ?? reserveUpload(bytes.length);
        if (!held.active || held.size !== bytes.length) {
          throw storeError(400, 'Invalid upload reservation');
        }
        let createdObject = false;
        try {
          const release = { ...report, uploadedAt: new Date().toISOString() };
          const objectPath = join(objects, `${report.packageSha256}.ota`);
          if (!objectSizes.has(report.packageSha256)) {
            await atomicWrite(fs, temporaryDirectory, objectPath, bytes);
            objectSizes.set(report.packageSha256, bytes.length);
            usedBytes += bytes.length;
            createdObject = true;
          }
          // Validate final disk bytes before a visible record is created.
          await readVerified(release);
          await atomicWrite(fs, temporaryDirectory, join(records, `${report.packageId}.json`), `${JSON.stringify(release)}\n`);
          releases.set(report.packageId, release);
          versions.set(report.version, report.packageId);
          return { release: copy(release), created: true };
        } catch (error) {
          if (createdObject) {
            try {
              await fs.unlink(join(objects, `${report.packageSha256}.ota`));
              objectSizes.delete(report.packageSha256);
              usedBytes -= bytes.length;
            } catch {
              // A failed cleanup still occupies quota and never becomes a visible release.
            }
          }
          throw error;
        } finally {
          held.release();
        }
      });
    },
    async publish(packageId) {
      return exclusive(async () => {
        const release = ID.test(packageId) ? releases.get(packageId) : undefined;
        if (!release) { throw storeError(404, 'Verified release was not found'); }
        await readVerified(release);
        const pointer = { packageId, packageSha256: release.packageSha256, publishedAt: new Date().toISOString() };
        await atomicWrite(fs, temporaryDirectory, join(root, 'current.json'), `${JSON.stringify(pointer)}\n`);
        current = { ...release, publishedAt: pointer.publishedAt };
        return copy(current);
      });
    },
    async read(packageId) {
      const release = ID.test(packageId) ? releases.get(packageId) : undefined;
      if (!release) { throw storeError(404, 'Verified release was not found'); }
      return readVerified(release);
    },
    async latest() {
      const selected = current;
      if (!selected) { return { available: false }; }
      const { bytes } = await readVerified(selected);
      return { available: true, version: selected.version, packageId: selected.packageId, header: bytes.subarray(0, HEADER_SIZE).toString('hex') };
    }
  };
}
