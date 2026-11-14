import { createHash, createHmac, timingSafeEqual } from 'node:crypto';

export const BOOT_BASE = 0x08000000;
export const APP_BASE = 0x08010000;
export const FOOTER_BASE = 0x0807ff00;
export const HEADER_SIZE = 256;
export const IMAGE_MAX = 458496;
export const FOOTER_COMMIT = 0x3141544f;
const MAGIC = Buffer.from('CLKOTA1\0', 'ascii');
const BOARD = Buffer.alloc(32);
BOARD.write('Clock_CubeMX-STM32F407VE', 'ascii');

export function releaseKey(value) {
  const key = typeof value === 'string' && /^[0-9a-fA-F]{64}$/.test(value) ? Buffer.from(value, 'hex') : value;
  if (!Buffer.isBuffer(key) || key.length !== 32 || key.every(byte => byte === 0)) {
    throw new Error('Release key must be a nonzero 32-byte value');
  }
  return Buffer.from(key);
}

function sha256(bytes) {
  return createHash('sha256').update(bytes).digest();
}

function equal(left, right) {
  return left.length === right.length && timingSafeEqual(left, right);
}

function validImageSize(size) {
  return Number.isInteger(size) && size >= 0x188 && size <= IMAGE_MAX;
}

export function validateVectors(image, base = APP_BASE, maximum = IMAGE_MAX) {
  if (!Buffer.isBuffer(image) || image.length < 0x188 || image.length > maximum) {
    throw new Error('Image size is outside its Flash region');
  }
  const stack = image.readUInt32LE(0);
  const reset = image.readUInt32LE(4);
  const address = reset - (reset & 1);
  if ((stack & 7) !== 0 || stack <= 0x20000000 || stack > 0x20020000) {
    throw new Error('Image initial stack is outside aligned SRAM');
  }
  if ((reset & 1) === 0 || address < base + 0x188 || address >= base + image.length) {
    throw new Error('Image reset vector is outside its linked Flash region');
  }
}

export function createPackage(image, keyValue, version) {
  const key = releaseKey(keyValue);
  validateVectors(image);
  if (!Number.isInteger(version) || version < 1 || version > 0xffffffff) {
    throw new Error('Firmware version must be a positive u32 integer');
  }
  const header = Buffer.alloc(HEADER_SIZE);
  MAGIC.copy(header);
  header.writeUInt32LE(1, 8);
  header.writeUInt32LE(HEADER_SIZE, 12);
  header.writeUInt32LE(APP_BASE, 16);
  header.writeUInt32LE(image.length, 20);
  header.writeUInt32LE(version, 24);
  BOARD.copy(header, 32);
  sha256(image).copy(header, 64);
  sha256(header.subarray(0, 96)).copy(header, 96, 0, 16);
  createHmac('sha256', key).update(header.subarray(0, 192)).digest().copy(header, 192);
  header.writeUInt32LE(0xffffffff, 252);
  return Buffer.concat([header, image]);
}

export function parseHeader(header, keyValue, { installed = false } = {}) {
  const key = releaseKey(keyValue);
  if (!Buffer.isBuffer(header) || header.length !== HEADER_SIZE) {
    throw new Error('Manifest must contain exactly 256 bytes');
  }
  if (!equal(header.subarray(0, 8), MAGIC) || header.readUInt32LE(8) !== 1 || header.readUInt32LE(12) !== HEADER_SIZE) {
    throw new Error('Unsupported manifest format');
  }
  if (header.readUInt32LE(16) !== APP_BASE || !validImageSize(header.readUInt32LE(20)) || header.readUInt32LE(24) === 0) {
    throw new Error('Invalid manifest image address, size or version');
  }
  if (header.readUInt32LE(28) !== 0 || !equal(header.subarray(32, 64), BOARD)) {
    throw new Error('Invalid manifest flags or target board');
  }
  if (!header.subarray(112, 192).every(byte => byte === 0) || !header.subarray(224, 252).every(byte => byte === 0)) {
    throw new Error('Manifest reserved fields must be zero');
  }
  if (header.readUInt32LE(252) !== (installed ? FOOTER_COMMIT : 0xffffffff)) {
    throw new Error('Invalid manifest commit marker');
  }
  const identity = sha256(header.subarray(0, 96)).subarray(0, 16);
  const authentication = createHmac('sha256', key).update(header.subarray(0, 192)).digest();
  if (!equal(header.subarray(96, 112), identity) || !equal(header.subarray(192, 224), authentication)) {
    throw new Error('Manifest authentication failed');
  }
  return {
    imageSize: header.readUInt32LE(20),
    version: header.readUInt32LE(24),
    digest: Buffer.from(header.subarray(64, 96)),
    id: Buffer.from(header.subarray(96, 112)),
    header: Buffer.from(header)
  };
}

export function verifyPackage(bytes, keyValue) {
  if (!Buffer.isBuffer(bytes) || bytes.length < HEADER_SIZE) {
    throw new Error('Package is shorter than its manifest');
  }
  const manifest = parseHeader(bytes.subarray(0, HEADER_SIZE), keyValue);
  if (bytes.length !== HEADER_SIZE + manifest.imageSize) {
    throw new Error('Package body length does not match manifest');
  }
  const image = bytes.subarray(HEADER_SIZE);
  if (!equal(sha256(image), manifest.digest)) {
    throw new Error('Image SHA-256 does not match manifest');
  }
  validateVectors(image);
  return { ...manifest, image: Buffer.from(image) };
}

function hexRecord(type, address, data) {
  const bytes = Buffer.alloc(data.length + 5);
  bytes[0] = data.length;
  bytes.writeUInt16BE(address, 1);
  bytes[3] = type;
  data.copy(bytes, 4);
  bytes[bytes.length - 1] = -bytes.subarray(0, -1).reduce((sum, value) => sum + value, 0) & 0xff;
  return `:${bytes.toString('hex').toUpperCase()}`;
}

export function createFactoryHex(boot, packageBytes, keyValue) {
  const manifest = verifyPackage(packageBytes, keyValue);
  validateVectors(boot, BOOT_BASE, APP_BASE - BOOT_BASE);
  const footer = Buffer.from(manifest.header);
  footer.writeUInt32LE(FOOTER_COMMIT, 252);
  const lines = [];
  let lastUpper = -1;
  for (const [base, bytes] of [[BOOT_BASE, boot], [APP_BASE, manifest.image], [FOOTER_BASE, footer]]) {
    for (let offset = 0; offset < bytes.length; offset += 16) {
      const address = base + offset;
      const upper = Math.floor(address / 65536);
      if (upper !== lastUpper) {
        const data = Buffer.alloc(2);
        data.writeUInt16BE(upper);
        lines.push(hexRecord(4, 0, data));
        lastUpper = upper;
      }
      lines.push(hexRecord(0, address & 0xffff, bytes.subarray(offset, offset + 16)));
    }
  }
  lines.push(':00000001FF');
  return `${lines.join('\n')}\n`;
}

export function packageReport(packageBytes, keyValue) {
  const manifest = verifyPackage(packageBytes, keyValue);
  return {
    board: 'Clock_CubeMX-STM32F407VE',
    version: manifest.version,
    appAddress: '0x08010000',
    footerAddress: '0x0807FF00',
    imageSize: manifest.imageSize,
    packageSize: packageBytes.length,
    imageSha256: manifest.digest.toString('hex'),
    packageSha256: sha256(packageBytes).toString('hex'),
    packageId: manifest.id.toString('hex')
  };
}
