import { createServer } from 'node:http';
import { createHash, timingSafeEqual } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { packageQuery, respondPackage, validatePackagePath } from './server.mjs';
import { PACKAGE_MAX, storeError } from './release-store.mjs';

const staticFiles = new Map([
  ['/', ['index.html', 'text/html; charset=utf-8']],
  ['/app.mjs', ['app.mjs', 'text/javascript; charset=utf-8']],
  ['/style.css', ['style.css', 'text/css; charset=utf-8']]
]);

function json(res, status, value, head = false) {
  const body = Buffer.from(JSON.stringify(value));
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': body.length });
  res.end(head ? undefined : body);
}

function empty(res, status, headers = {}) {
  res.writeHead(status, { ...headers, 'Content-Length': 0 });
  res.end();
}

function readUpload(req, res, length, deadline) {
  return new Promise((done, reject) => {
    const chunks = [];
    let received = 0;
    let settled = false;
    const timeout = setTimeout(() => finish(storeError(408, 'Upload deadline exceeded')), deadline);
    function finish(error, bytes) {
      if (settled) { return; }
      settled = true;
      clearTimeout(timeout);
      req.removeListener('data', onData);
      req.removeListener('end', onEnd);
      req.removeListener('aborted', onAbort);
      req.removeListener('error', onAbort);
      res.removeListener('close', onClose);
      if (error) {
        req.resume();
        reject(error);
      } else {
        done(bytes);
      }
    }
    function onData(chunk) {
      received += chunk.length;
      if (received > length || received > PACKAGE_MAX) {
        finish(storeError(413, 'Upload exceeds declared or maximum package size'));
      } else {
        chunks.push(chunk);
      }
    }
    function onEnd() {
      if (received !== length) {
        finish(storeError(400, 'Upload was truncated'));
      } else {
        finish(undefined, Buffer.concat(chunks, received));
      }
    }
    function onAbort() { finish(storeError(400, 'Upload was interrupted')); }
    function onClose() {
      if (!req.complete) { finish(storeError(400, 'Upload connection closed')); }
    }
    req.on('data', onData);
    req.once('end', onEnd);
    req.once('aborted', onAbort);
    req.once('error', onAbort);
    res.once('close', onClose);
  });
}

export function createAdminServer({ store, adminTokenHex, path = '/firmware.ota', uploadDeadlineMs = 30000, maxConcurrentUploads = 2 }) {
  validatePackagePath(path);
  if (Buffer.byteLength(path) > 156 || staticFiles.has(path) || path.startsWith('/api/') || path === '/api') {
    throw new Error('OTA package path must be distinct from management paths and at most 156 bytes');
  }
  if (!/^[0-9a-fA-F]{64}$/.test(adminTokenHex ?? '') || /^0+$/.test(adminTokenHex)) {
    throw new Error('A nonzero 32-byte management token is required');
  }
  if (!Number.isSafeInteger(uploadDeadlineMs) || uploadDeadlineMs < 1 || !Number.isSafeInteger(maxConcurrentUploads) || maxConcurrentUploads < 1 || maxConcurrentUploads > 2) {
    throw new Error('Invalid upload bounds');
  }
  const tokenDigest = createHash('sha256').update(Buffer.from(adminTokenHex, 'hex')).digest();
  const assets = new Map([...staticFiles].map(([route, [file, type]]) => [route, { bytes: readFileSync(new URL(`./admin/${file}`, import.meta.url)), type }]));
  let uploads = 0;
  const server = createServer({ maxHeaderSize: 4096 }, (req, res) => {
    res.setHeader('Connection', 'close');
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('X-Content-Type-Options', 'nosniff');
    res.setHeader('Referrer-Policy', 'no-referrer');
    res.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    handle(req, res).catch(error => {
      if (res.destroyed || res.writableEnded) { return; }
      if (res.headersSent) { res.destroy(); return; }
      json(res, error.status ?? 503, { error: error.status ? error.message : 'Persistent storage is unavailable; current release was preserved' }, req.method === 'HEAD');
    });
  });

  async function handle(req, res) {
    const staticAsset = assets.get(req.url);
    if (staticAsset) {
      if (!['GET', 'HEAD'].includes(req.method)) { empty(res, 405, { Allow: 'GET, HEAD' }); return; }
      res.writeHead(200, { 'Content-Type': staticAsset.type, 'Content-Length': staticAsset.bytes.length });
      res.end(req.method === 'HEAD' ? undefined : staticAsset.bytes);
      return;
    }
    const query = packageQuery(req.url, path);
    if (query) {
      if (!['GET', 'HEAD'].includes(req.method)) { empty(res, 405, { Allow: 'GET, HEAD' }); return; }
      if (query.kind === 'latest') {
        json(res, 200, await store.latest(), req.method === 'HEAD');
        return;
      }
      const packageId = query.kind === 'id' ? query.packageId : (await store.list()).current?.packageId;
      if (!packageId) { throw storeError(404, 'No release has been published'); }
      const { bytes, release } = await store.read(packageId);
      respondPackage(req, res, bytes, `"${release.packageSha256}"`);
      return;
    }
    const publish = /^\/api\/releases\/([0-9a-f]{32})\/publish$/.exec(req.url);
    if (req.url !== '/api/releases' && !publish) { empty(res, 404); return; }
    if (req.headers.origin !== undefined && req.headers.origin !== `http://${req.headers.host}`) {
      throw storeError(403, 'Management requests must have the same origin');
    }
    const supplied = /^Bearer ([0-9a-fA-F]{64})$/.exec(req.headers.authorization ?? '');
    const candidate = createHash('sha256').update(supplied ? Buffer.from(supplied[1], 'hex') : Buffer.alloc(32)).digest();
    if (!timingSafeEqual(candidate, tokenDigest) || !supplied) {
      res.setHeader('WWW-Authenticate', 'Bearer');
      throw storeError(401, 'A valid management token is required');
    }
    if (publish) {
      if (req.method !== 'POST') { empty(res, 405, { Allow: 'POST' }); return; }
      if (req.headers['transfer-encoding'] || (req.headers['content-length'] !== undefined && req.headers['content-length'] !== '0')) {
        throw storeError(400, 'Publish requests must have an empty body');
      }
      json(res, 200, { release: await store.publish(publish[1]) });
      return;
    }
    if (req.method === 'GET') {
      json(res, 200, await store.list());
      return;
    }
    if (req.method !== 'POST') { empty(res, 405, { Allow: 'GET, POST' }); return; }
    if (req.headers['content-type'] !== 'application/octet-stream' || req.headers['content-encoding'] !== undefined) {
      throw storeError(415, 'Upload must be uncompressed application/octet-stream');
    }
    const declared = req.headers['content-length'];
    if (req.headers['transfer-encoding'] || declared === undefined) {
      throw storeError(411, 'An exact Content-Length is required');
    }
    const length = Number(declared);
    if (!/^\d+$/.test(declared) || !Number.isSafeInteger(length) || length < 1 || length > PACKAGE_MAX) {
      throw storeError(413, 'Package must be between 1 and 458752 bytes');
    }
    if (uploads >= maxConcurrentUploads) { throw storeError(429, 'Two uploads are already in progress; retry later'); }
    const reservation = await store.reserveUpload(length);
    uploads++;
    try {
      const bytes = await readUpload(req, res, length, uploadDeadlineMs);
      const result = await store.upload(bytes, { reservation });
      json(res, result.created ? 201 : 200, result);
    } finally {
      reservation.release();
      uploads--;
    }
  }
  server.requestTimeout = 31000;
  server.headersTimeout = 10000;
  server.keepAliveTimeout = 1;
  return server;
}
