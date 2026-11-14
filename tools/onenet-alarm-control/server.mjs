import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const UPSTREAM_READ = 'https://iot-api.heclouds.com/thingmodel/query-device-property-detail';
const UPSTREAM_SET = 'https://iot-api.heclouds.com/thingmodel/set-device-property';
const ALARM_NAMES = Array.from({ length: 8 }, (_, index) => `alarm${index + 1}`);
const ALARM_FIELDS = [
  'enabled', 'hour', 'minute', 'repeat_mask', 'year', 'month', 'day',
  'ring_seconds', 'snooze_seconds',
];
const STATIC_FILES = new Map([
  ['/', ['index.html', 'text/html; charset=utf-8']],
  ['/index.html', ['index.html', 'text/html; charset=utf-8']],
  ['/app.mjs', ['app.mjs', 'text/javascript; charset=utf-8']],
  ['/model.mjs', ['model.mjs', 'text/javascript; charset=utf-8']],
]);

function sendJson(response, status, value) {
  response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8' });
  response.end(JSON.stringify(value));
}

function fail(response, status, error, message) {
  sendJson(response, status, { error, message });
}

function hasExactKeys(value, keys) {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
    && Object.keys(value).length === keys.length
    && keys.every((key) => Object.hasOwn(value, key));
}

function integerBetween(value, minimum, maximum) {
  return Number.isInteger(value) && value >= minimum && value <= maximum;
}

function validAlarm(alarm) {
  if (!hasExactKeys(alarm, ALARM_FIELDS)
    || typeof alarm.enabled !== 'boolean'
    || !integerBetween(alarm.hour, 0, 23)
    || !integerBetween(alarm.minute, 0, 59)
    || !integerBetween(alarm.repeat_mask, 0, 127)
    || !integerBetween(alarm.year, 2020, 2099)
    || !integerBetween(alarm.month, 1, 12)
    || !integerBetween(alarm.day, 1, 31)
    || !integerBetween(alarm.ring_seconds, 5, 600)
    || !integerBetween(alarm.snooze_seconds, 60, 1800)
    || alarm.snooze_seconds % 60 !== 0) return false;
  return alarm.day <= new Date(Date.UTC(alarm.year, alarm.month, 0)).getUTCDate();
}

function validBody(body, isWrite) {
  const fields = isWrite ? ['product_id', 'device_name', 'params'] : ['product_id', 'device_name'];
  if (!hasExactKeys(body, fields)
    || typeof body.product_id !== 'string' || !/^[A-Za-z0-9]{8,32}$/.test(body.product_id)
    || typeof body.device_name !== 'string' || !/^[A-Za-z0-9_-]{1,64}$/.test(body.device_name)) return false;
  if (!isWrite) return true;
  const params = body.params;
  if (params === null || typeof params !== 'object' || Array.isArray(params)) return false;
  const names = Object.keys(params);
  return names.length === 1 && ALARM_NAMES.includes(names[0]) && validAlarm(params[names[0]]);
}

function validToken(token) {
  if (typeof token !== 'string' || !token.trim() || token.length > 4096 || /[\r\n]/.test(token)) return false;
  const fields = new URLSearchParams(token);
  return fields.get('version') === '2022-05-01'
    && ['res', 'et', 'method', 'sign'].every((name) => Boolean(fields.get(name)?.trim()));
}

function localRequest(request) {
  const host = request.headers.host;
  const match = typeof host === 'string' && /^(localhost|127\.0\.0\.1)(?::(\d+))?$/i.exec(host);
  if (!match || Number(match[2] ?? 80) !== request.socket.localPort) return false;
  const origin = request.headers.origin;
  if (origin === undefined) return true;
  // localhost and 127.0.0.1 are separate browser origins, even on the same port.
  return origin === new URL(`http://${host}`).origin;
}

function readJson(request) {
  return new Promise((resolveBody, reject) => {
    let size = 0;
    let rejected = false;
    const chunks = [];
    request.on('data', (chunk) => {
      if (rejected) return;
      size += chunk.length;
      if (size > 8192) {
        rejected = true;
        chunks.length = 0;
        reject(Object.assign(new Error('Request body too large'), { status: 413 }));
        return;
      }
      chunks.push(chunk);
    });
    request.on('end', () => {
      if (rejected) return;
      try {
        resolveBody(JSON.parse(Buffer.concat(chunks).toString('utf8')));
      } catch {
        reject(Object.assign(new Error('Invalid JSON'), { status: 400 }));
      }
    });
    request.on('error', () => reject(Object.assign(new Error('Incomplete request'), { status: 400 })));
    request.on('aborted', () => reject(Object.assign(new Error('Incomplete request'), { status: 400 })));
  });
}

/** Build a local-only API bridge. No token, request body or upstream error is logged. */
export function createControlServer({ fetchImpl = fetch, upstreamTimeoutMs = 12000 } = {}) {
  if (typeof fetchImpl !== 'function' || !Number.isFinite(upstreamTimeoutMs) || upstreamTimeoutMs <= 0) {
    throw new TypeError('A fetch function and positive upstream timeout are required.');
  }
  const recentWrites = new Map();
  return http.createServer(async (request, response) => {
    response.setHeader('Cache-Control', 'no-store');
    response.setHeader('X-Content-Type-Options', 'nosniff');
    if (!localRequest(request)) {
      request.resume();
      fail(response, 403, 'forbidden', 'Only same-origin loopback requests are allowed.');
      return;
    }

    const staticFile = STATIC_FILES.get(request.url);
    if (staticFile && (request.method === 'GET' || request.method === 'HEAD')) {
      try {
        // The filename comes exclusively from the fixed map, never from a request path.
        const content = await readFile(new URL(staticFile[0], import.meta.url));
        response.writeHead(200, { 'Content-Type': staticFile[1] });
        response.end(request.method === 'HEAD' ? undefined : content);
      } catch {
        fail(response, 404, 'not_found', 'Page not found.');
      }
      return;
    }

    const isWrite = request.url === '/api/onenet/set';
    if (!isWrite && request.url !== '/api/onenet/read') {
      request.resume();
      fail(response, 404, 'not_found', 'Route not found.');
      return;
    }
    if (request.method !== 'POST') {
      request.resume();
      response.setHeader('Allow', 'POST');
      fail(response, 405, 'method_not_allowed', 'Use POST for this API.');
      return;
    }
    if (request.headers['x-clock-control'] !== '1') {
      request.resume();
      fail(response, 403, 'forbidden', 'The local control header is required.');
      return;
    }
    if (request.headers['content-type']?.split(';')[0].trim().toLowerCase() !== 'application/json') {
      request.resume();
      fail(response, 415, 'unsupported_media_type', 'Use application/json.');
      return;
    }
    const token = request.headers.authorization;
    if (!validToken(token)) {
      request.resume();
      fail(response, 401, 'invalid_authorization', 'Enter a complete version=2022-05-01 OneNET API token.');
      return;
    }

    let body;
    try {
      body = await readJson(request);
    } catch (error) {
      fail(response, error.status === 413 ? 413 : 400, 'invalid_request', 'The JSON body must be valid and at most 8192 bytes.');
      return;
    }
    if (!validBody(body, isWrite)) {
      fail(response, 400, 'invalid_request', 'Invalid device identifiers or alarm fields. Supply exactly one complete alarm when setting.');
      return;
    }
    if (isWrite) {
      const now = performance.now();
      for (const [key, time] of recentWrites) {
        if (now - time >= 1000) recentWrites.delete(key);
      }
      const key = `${body.product_id}/${body.device_name}`;
      if (recentWrites.has(key)) {
        response.setHeader('Retry-After', '1');
        fail(response, 429, 'rate_limited', 'Wait at least one second before setting this device again.');
        return;
      }
      recentWrites.set(key, now);
    }

    const controller = new AbortController();
    let timer;
    let timedOut = false;
    try {
      const timeout = new Promise((_, reject) => {
        timer = setTimeout(() => {
          timedOut = true;
          controller.abort();
          reject(new Error('Upstream timeout'));
        }, upstreamTimeoutMs);
      });
      const work = (async () => {
        const upstream = await fetchImpl(isWrite ? UPSTREAM_SET : UPSTREAM_READ, {
          method: 'POST',
          headers: { Authorization: token, 'Content-Type': 'application/json' },
          body: JSON.stringify(isWrite ? body : { ...body, params: ALARM_NAMES }),
          redirect: 'error',
          signal: controller.signal,
        });
        const text = await upstream.text();
        JSON.parse(text); // Validate JSON without changing its two result-code levels or values.
        if (!integerBetween(upstream.status, 200, 599)) throw new Error('Invalid upstream status');
        return { status: upstream.status, text };
      })();
      const upstream = await Promise.race([work, timeout]);
      response.writeHead(upstream.status, { 'Content-Type': 'application/json; charset=utf-8' });
      response.end(upstream.text);
    } catch {
      // A write may already have reached the device. Never retry an uncertain request.
      if (timedOut) {
        fail(response, 504, 'upstream_timeout', 'OneNET timed out. The result is unconfirmed; read the device state before another write.');
      } else {
        fail(response, 502, 'result_unconfirmed', 'A valid OneNET response was not received. Read the device state before another write.');
      }
    } finally {
      clearTimeout(timer);
    }
  });
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const portText = process.env.CLOCK_ALARM_CONTROL_PORT ?? '8787';
  const port = Number(portText);
  if (!/^\d+$/.test(portText) || !integerBetween(port, 1, 65535)) {
    process.stderr.write('CLOCK_ALARM_CONTROL_PORT must be an integer from 1 to 65535.\n');
    process.exitCode = 1;
  } else {
    const server = createControlServer();
    server.on('error', () => {
      process.stderr.write('Unable to start the local control server. Check whether the selected port is available.\n');
      process.exitCode = 1;
    });
    server.listen(port, '127.0.0.1', () => {
      console.log(`http://127.0.0.1:${port}`);
    });
  }
}
