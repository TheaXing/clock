import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
import { verifyPackage } from './protocol.mjs';

export function validatePackagePath(path) {
  if (typeof path !== 'string' || !/^\/[A-Za-z0-9._/-]+$/.test(path) || path.includes('//') || path.split('/').some(part => part === '.' || part === '..')) {
    throw new Error('Package URL must be a fixed absolute path without traversal or query');
  }
  return path;
}

function parseRange(range, length) {
  const match = /^bytes=(\d*)-(\d*)$/.exec(range);
  if (!match || (!match[1] && !match[2])) {
    return null;
  }
  const first = match[1] ? Number(match[1]) : null;
  const last = match[2] ? Number(match[2]) : null;
  if ((first !== null && !Number.isSafeInteger(first)) || (last !== null && !Number.isSafeInteger(last))) {
    return null;
  }
  if (first === null) {
    return last > 0 ? [Math.max(0, length - last), length - 1] : null;
  }
  if (first >= length || (last !== null && last < first)) {
    return null;
  }
  return [first, Math.min(last ?? length - 1, length - 1)];
}

export function packageQuery(url, path) {
  if (url === path) { return { kind: 'alias' }; }
  if (url === `${path}?latest=1`) { return { kind: 'latest' }; }
  if (url?.startsWith(`${path}?id=`) && /^[0-9a-f]{32}$/.test(url.slice(path.length + 4))) {
    return { kind: 'id', packageId: url.slice(path.length + 4) };
  }
  return null;
}

export function respondPackage(req, res, snapshot, etag) {
  res.setHeader('ETag', etag);
  res.setHeader('Accept-Ranges', 'bytes');
  res.setHeader('Content-Type', 'application/octet-stream');
  if (req.headers['if-match'] !== undefined && req.headers['if-match'] !== etag) {
    res.writeHead(412, { 'Content-Length': 0 });
    res.end();
    return;
  }
  let first = 0;
  let last = snapshot.length - 1;
  let status = 200;
  if (req.headers.range !== undefined) {
    const range = parseRange(req.headers.range, snapshot.length);
    if (!range) {
      res.writeHead(416, { 'Content-Range': `bytes */${snapshot.length}`, 'Content-Length': 0 });
      res.end();
      return;
    }
    [first, last] = range;
    status = 206;
    res.setHeader('Content-Range', `bytes ${first}-${last}/${snapshot.length}`);
  }
  res.writeHead(status, { 'Content-Length': last - first + 1 });
  res.end(req.method === 'HEAD' ? undefined : snapshot.subarray(first, last + 1));
}

export function createOtaServer({ packageBytes, key, path = '/firmware.ota' }) {
  validatePackagePath(path);
  const manifest = verifyPackage(packageBytes, key);
  const snapshot = Buffer.from(packageBytes);
  const etag = `"${createHash('sha256').update(snapshot).digest('hex')}"`;
  const server = createServer({ maxHeaderSize: 4096 }, (req, res) => {
    res.setHeader('Connection', 'close');
    res.setHeader('Cache-Control', 'no-store');
    const query = packageQuery(req.url, path);
    if (!query || (query.kind === 'id' && query.packageId !== manifest.id.toString('hex'))) {
      res.writeHead(404, { 'Content-Length': 0 });
      res.end();
      return;
    }
    if (req.method !== 'GET' && req.method !== 'HEAD') {
      res.writeHead(405, { Allow: 'GET, HEAD', 'Content-Length': 0 });
      res.end();
      return;
    }
    if (query.kind === 'latest') {
      const body = Buffer.from(JSON.stringify({ available: true, version: manifest.version, packageId: manifest.id.toString('hex'), header: manifest.header.toString('hex') }));
      res.writeHead(200, { 'Content-Type': 'application/json', 'Content-Length': body.length });
      res.end(req.method === 'HEAD' ? undefined : body);
      return;
    }
    respondPackage(req, res, snapshot, etag);
  });
  server.requestTimeout = 15000;
  server.headersTimeout = 10000;
  server.keepAliveTimeout = 1;
  return server;
}
