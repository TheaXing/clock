export const ALARM_COUNT = 8;
export const ALARM_FIELDS = Object.freeze([
  'enabled', 'hour', 'minute', 'repeat_mask', 'year', 'month', 'day',
  'ring_seconds', 'snooze_seconds',
]);

const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const pad = value => String(value).padStart(2, '0');
const text = (value, fallback = '') => typeof value === 'string' ? value.slice(0, 500) : fallback;

function integer(value, min, max, name) {
  if (!Number.isInteger(value) || value < min || value > max) {
    throw new Error(`${name}应为 ${min}–${max} 范围内的整数。`);
  }
}

export function validateAlarm(alarm) {
  if (!object(alarm) || Object.keys(alarm).length !== ALARM_FIELDS.length ||
      ALARM_FIELDS.some(key => !Object.hasOwn(alarm, key))) {
    throw new Error('闹钟配置必须包含完整的 9 个字段。');
  }
  if (typeof alarm.enabled !== 'boolean') throw new Error('闹钟启用状态必须为布尔值。');
  integer(alarm.hour, 0, 23, '小时');
  integer(alarm.minute, 0, 59, '分钟');
  integer(alarm.repeat_mask, 0, 127, '重复星期');
  integer(alarm.year, 2020, 2099, '年份');
  integer(alarm.month, 1, 12, '月份');
  integer(alarm.day, 1, 31, '日期');
  const calendar = new Date(Date.UTC(alarm.year, alarm.month - 1, alarm.day));
  if (calendar.getUTCFullYear() !== alarm.year || calendar.getUTCMonth() !== alarm.month - 1 ||
      calendar.getUTCDate() !== alarm.day) throw new Error('指定日期不存在，请重新选择日期。');
  integer(alarm.ring_seconds, 5, 600, '响铃秒数');
  integer(alarm.snooze_seconds, 60, 1800, '贪睡秒数');
  if (alarm.snooze_seconds % 60 !== 0) throw new Error('贪睡时长应为 1–30 整分钟。');
  return Object.fromEntries(ALARM_FIELDS.map(key => [key, alarm[key]]));
}

export function validateDevice(device) {
  if (!object(device) || typeof device.product_id !== 'string' ||
      !/^[A-Za-z0-9]{8,32}$/.test(device.product_id)) throw new Error('请填写正确的产品 ID。');
  if (typeof device.device_name !== 'string' || !/^[A-Za-z0-9_-]{1,64}$/.test(device.device_name)) {
    throw new Error('请填写正确的设备名称。');
  }
  return { product_id: device.product_id, device_name: device.device_name };
}

export function buildSetPayload(device, alarmNumber, alarm) {
  const target = validateDevice(device);
  integer(alarmNumber, 1, ALARM_COUNT, '闹钟组号');
  return { ...target, params: { [`alarm${alarmNumber}`]: validateAlarm(alarm) } };
}

export function classifySetResponse(body) {
  const validBody = object(body);
  const data = validBody && object(body.data) ? body.data : null;
  const platformCode = validBody && Number.isInteger(body.code) ? body.code : null;
  const deviceCode = data && Number.isInteger(data.code) ? data.code : null;
  const common = {
    platformCode, deviceCode,
    requestId: validBody ? text(body.request_id) : '',
    commandId: data ? text(data.id) : '',
  };
  if (platformCode === 0 && deviceCode === 200) {
    return { ...common, kind: 'confirmed', message: '开发板已确认闹钟保存成功。' };
  }
  if (platformCode === 0 && data?.msg === 'result_unknown') {
    return { ...common, kind: 'unconfirmed', message: '开发板无法确认保存结果，请先读取核对。' };
  }
  if (platformCode === 0 && deviceCode !== null) {
    const reasons = {
      save_failed: '开发板保存失败，请检查存储状态后读取核对。',
      busy_or_conflict: '开发板正忙或配置已变更，请读取最新配置后再操作。',
      busy: '开发板正忙，请稍后读取配置再操作。',
      request_expired: '指令到达后已超时，开发板未执行本次设置。',
      invalid_alarm_request: '开发板拒绝了闹钟参数，请检查物模型和各项取值。',
      alarm_unavailable: '开发板的闹钟服务当前不可用。',
    };
    return { ...common, kind: 'rejected', message: reasons[data.msg] || text(data.msg, `开发板返回错误码 ${deviceCode}，未确认保存。`) };
  }
  return {
    ...common, kind: 'unconfirmed',
    message: platformCode !== 0 && validBody && body.msg
      ? `未收到开发板保存确认：${text(body.msg)}`
      : '平台没有返回开发板保存确认，请先读取开发板核对结果。',
  };
}

export function parseReadResponse(body) {
  if (!object(body) || body.code !== 0) {
    throw new Error(object(body) ? text(body.msg, '读取开发板失败。') : '平台返回的数据无法识别。');
  }
  if (!object(body.data)) throw new Error('平台未返回开发板的闹钟属性。');
  const alarms = Array(ALARM_COUNT).fill(null);
  const invalid = [];
  for (let index = 0; index < ALARM_COUNT; index += 1) {
    const key = `alarm${index + 1}`;
    if (!Object.hasOwn(body.data, key)) continue;
    try {
      const value = typeof body.data[key] === 'string' ? JSON.parse(body.data[key]) : body.data[key];
      alarms[index] = validateAlarm(value);
    } catch {
      invalid.push(index + 1);
    }
  }
  const count = alarms.filter(Boolean).length;
  if (!count) throw new Error('没有读到符合当前固件格式的闹钟，请检查设备在线状态和物模型定义。');
  return { alarms, invalid, count, requestId: text(body.request_id) };
}

export function defaultAlarm(now = new Date()) {
  const date = new Date(now.getTime() + 8 * 60 * 60 * 1000);
  return {
    enabled: true, hour: 7, minute: 0, repeat_mask: 31,
    year: date.getUTCFullYear(), month: date.getUTCMonth() + 1, day: date.getUTCDate(),
    ring_seconds: 30, snooze_seconds: 300,
  };
}

export function formatTime(alarm) {
  return `${pad(alarm.hour)}:${pad(alarm.minute)}`;
}

export function formatDate(alarm) {
  return `${alarm.year}-${pad(alarm.month)}-${pad(alarm.day)}`;
}

export function formatRepeat(alarm) {
  if (alarm.repeat_mask === 0) return `${formatDate(alarm)} · 仅一次`;
  if (alarm.repeat_mask === 127) return '每天';
  if (alarm.repeat_mask === 31) return '工作日';
  if (alarm.repeat_mask === 96) return '周末';
  return `每周${['一', '二', '三', '四', '五', '六', '日']
    .filter((_, index) => alarm.repeat_mask & (1 << index)).join('、')}`;
}

export function sameAlarm(left, right) {
  return object(left) && object(right) && ALARM_FIELDS.every(key => left[key] === right[key]);
}

export function validateApiToken(value, nowSeconds = Math.floor(Date.now() / 1000)) {
  const token = typeof value === 'string' ? value.trim() : '';
  if (!token) return { valid: false, reason: '请先填写 OneNET API Token。' };
  if (token.length > 4096 || /[\r\n]/.test(token)) return { valid: false, reason: 'API Token 格式不正确。' };
  const params = new URLSearchParams(token);
  if (params.get('version') !== '2022-05-01' || !params.get('res') || !params.get('method') || !params.get('sign')) {
    return { valid: false, reason: '请使用 2022-05-01 版 API Token，设备 MQTT 密钥或 Token 不适用于云端控制。' };
  }
  if (params.get('res').includes('/devices/')) return { valid: false, reason: '请使用用户级或产品级 API Token，不能使用设备级 MQTT Token。' };
  const expires = params.get('et');
  if (!/^\d+$/.test(expires || '') || !Number.isSafeInteger(Number(expires))) {
    return { valid: false, reason: 'API Token 缺少有效的到期时间。' };
  }
  if (Number(expires) <= nowSeconds) return { valid: false, reason: 'API Token 已过期，请重新生成。' };
  return { valid: true, reason: '' };
}

/** Sign only in this browser. The AccessKey is never sent to the local bridge. */
export async function generateUserApiToken(userId, accessKey, nowSeconds = Math.floor(Date.now() / 1000)) {
  const user = typeof userId === 'string' ? userId.trim() : '';
  const keyText = typeof accessKey === 'string' ? accessKey.trim() : '';
  if (!/^\d{1,20}$/.test(user)) throw new Error('请填写控制台中的数字用户 ID。');
  if (!keyText || keyText.length > 4096 ||
      !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(keyText)) {
    throw new Error('用户 AccessKey 格式不正确，请完整复制，不能填写设备密钥。');
  }
  if (!globalThis.crypto?.subtle) throw new Error('当前环境无法本机签名，请通过 localhost 或 127.0.0.1 打开页面。');
  if (!Number.isSafeInteger(nowSeconds) || nowSeconds < 0) throw new Error('当前电脑时间无效。');
  let keyBytes;
  try { keyBytes = Uint8Array.from(atob(keyText), character => character.charCodeAt(0)); }
  catch { throw new Error('用户 AccessKey 不是有效的 Base64 字符串。'); }
  if (!keyBytes.length) throw new Error('用户 AccessKey 不能为空。');
  const expires = nowSeconds + 3600;
  const resource = `userid/${user}`;
  try {
    const key = await globalThis.crypto.subtle.importKey('raw', keyBytes, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
    const signature = await globalThis.crypto.subtle.sign('HMAC', key,
      new TextEncoder().encode(`${expires}\nsha256\n${resource}\n2022-05-01`));
    const base64 = btoa(String.fromCharCode(...new Uint8Array(signature)));
    return `version=2022-05-01&res=${encodeURIComponent(resource)}&et=${expires}&method=sha256&sign=${encodeURIComponent(base64)}`;
  } finally {
    keyBytes.fill(0);
  }
}
