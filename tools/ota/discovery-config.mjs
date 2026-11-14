export function validateDiscoveryUrl(host, port, path) {
  const url = `http://${host}:${port}${path}?latest=1`;
  if (Buffer.byteLength(url, 'ascii') > 224) {
    throw new Error('Device discovery URL must be at most 224 ASCII bytes for the 255-byte HTTPCGET command');
  }
  return url;
}
