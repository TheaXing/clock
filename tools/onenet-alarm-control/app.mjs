import {
  ALARM_COUNT, buildSetPayload, classifySetResponse, defaultAlarm, formatDate,
  formatRepeat, formatTime, parseReadResponse, sameAlarm, validateAlarm,
  validateApiToken, validateDevice, generateUserApiToken,
} from './model.mjs';

const $ = id => document.getElementById(id);
const fields = {
  time: $('alarm-time'), enabled: $('alarm-enabled'), date: $('alarm-date'),
  ring: $('ring-seconds'), snooze: $('snooze-minutes'),
};
const weekdays = [...document.querySelectorAll('input[name="weekday"]')];
const modes = [...document.querySelectorAll('input[name="repeat-mode"]')];
const state = {
  selected: 1, alarms: Array(ALARM_COUNT).fill(null),
  drafts: new Map(), edited: new Set(), cardNotes: new Map(), busy: false,
};
const localFile = location.protocol === 'file:';
let generatingToken = false;
const target = () => ({ product_id: $('product-id').value.trim(), device_name: $('device-name').value.trim() });
const stamp = () => new Intl.DateTimeFormat('zh-CN', {
  timeZone: 'Asia/Shanghai', hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false,
}).format(new Date());

function message(element, text, tone = 'neutral') {
  element.textContent = text;
  element.dataset.tone = tone;
}

function snapshotForm() {
  return {
    time: fields.time.value, enabled: fields.enabled.checked, date: fields.date.value,
    mode: modes.find(input => input.checked)?.value || 'weekly',
    mask: weekdays.reduce((mask, input) => mask | (input.checked ? 1 << Number(input.value) : 0), 0),
    ring: fields.ring.value, snooze: fields.snooze.value,
  };
}

function formFromAlarm(alarm) {
  return {
    time: formatTime(alarm), enabled: alarm.enabled, date: formatDate(alarm),
    mode: alarm.repeat_mask ? 'weekly' : 'date', mask: alarm.repeat_mask,
    ring: String(alarm.ring_seconds), snooze: String(alarm.snooze_seconds / 60),
  };
}

function fillForm(form) {
  fields.time.value = form.time;
  fields.enabled.checked = form.enabled;
  fields.date.value = form.date;
  fields.ring.value = form.ring;
  fields.snooze.value = form.snooze;
  modes.forEach(input => { input.checked = input.value === form.mode; });
  weekdays.forEach(input => { input.checked = Boolean(form.mask & (1 << Number(input.value))); });
}

function currentAlarm() {
  const form = snapshotForm();
  if (!/^\d{2}:\d{2}$/.test(form.time)) throw new Error('请填写完整的闹钟时间。');
  if (!/^\d{4}-\d{2}-\d{2}$/.test(form.date)) throw new Error('请选择有效的日期。');
  if (form.mode === 'weekly' && !form.mask) throw new Error('请至少选择一个重复星期，或切换到指定日期。');
  const [hour, minute] = form.time.split(':').map(Number);
  const [year, month, day] = form.date.split('-').map(Number);
  return validateAlarm({
    enabled: form.enabled, hour, minute, repeat_mask: form.mode === 'date' ? 0 : form.mask,
    year, month, day, ring_seconds: Number(form.ring), snooze_seconds: Number(form.snooze) * 60,
  });
}

function connection() {
  const token = validateApiToken($('api-token').value);
  if (!token.valid) throw new Error(token.reason);
  return validateDevice(target());
}

function renderCards() {
  const grid = $('alarm-grid');
  grid.replaceChildren();
  state.alarms.forEach((alarm, index) => {
    const number = index + 1;
    const card = document.createElement('button');
    card.type = 'button';
    card.className = `alarm-card${number === state.selected ? ' selected' : ''}${alarm && !alarm.enabled ? ' is-off' : ''}`;
    card.setAttribute('aria-pressed', String(number === state.selected));
    card.setAttribute('aria-label', `编辑第 ${number} 组闹钟${alarm ? `，${formatTime(alarm)}，${alarm.enabled ? '已启用' : '已关闭'}` : '，尚未读取'}`);
    card.disabled = state.busy;
    for (const [className, content] of [
      ['alarm-label', `ALARM ${String(number).padStart(2, '0')}`],
      ['alarm-card-time', alarm ? formatTime(alarm) : '— : —'],
      ['alarm-card-repeat', alarm ? formatRepeat(alarm) : '尚未读取开发板'],
      ['alarm-card-state', state.cardNotes.get(number) || (state.edited.has(number) ? '有未下发的草稿' : alarm ? (alarm.enabled ? '已启用' : '已关闭') : '等待读取')],
    ]) {
      const span = document.createElement('span');
      span.className = className;
      span.textContent = content;
      card.append(span);
    }
    card.addEventListener('click', () => chooseAlarm(number));
    grid.append(card);
  });
  $('alarm-count').textContent = `${state.alarms.filter(Boolean).length} / 8 已读取或确认`;
}

function updateControls() {
  const mode = modes.find(input => input.checked)?.value || 'weekly';
  $('weekdays-panel').hidden = mode !== 'weekly';
  $('date-panel').hidden = mode !== 'date';
  document.querySelectorAll('input, button').forEach(element => { element.disabled = state.busy; });
  fields.date.disabled = state.busy || mode !== 'date';
  weekdays.forEach(input => { input.disabled = state.busy || mode !== 'weekly'; });
  const usableToken = validateApiToken($('api-token').value).valid;
  $('read-button').disabled = state.busy || localFile || !usableToken;
  $('read-back-button').disabled = state.busy || localFile || !usableToken;
  $('generate-token').disabled = state.busy || localFile || generatingToken;
  let valid = false;
  try { buildSetPayload(target(), state.selected, currentAlarm()); valid = true; } catch { /* A draft may be incomplete. */ }
  $('save-button').disabled = state.busy || localFile || !usableToken || !valid;
  $('save-button').textContent = state.busy ? '正在等待开发板…' : '下发并保存闹钟';
  $('alarm-form').setAttribute('aria-busy', String(state.busy));
}

function updateDraft() {
  let alarm;
  try {
    alarm = currentAlarm();
    const identical = sameAlarm(alarm, state.alarms[state.selected - 1]);
    message($('draft-badge'), identical ? '与设备一致' : '草稿 · 待下发', identical ? 'success' : 'warning');
    $('repeat-description').textContent = formatRepeat(alarm);
    $('draft-summary').textContent = `${formatTime(alarm)} · ${formatRepeat(alarm)} · ${alarm.enabled ? '启用' : '关闭'} · 响铃 ${alarm.ring_seconds} 秒`;
    $('request-preview').textContent = JSON.stringify(buildSetPayload(target(), state.selected, alarm), null, 2);
  } catch (error) {
    message($('draft-badge'), '请补全配置', 'warning');
    $('draft-summary').textContent = error.message;
    $('repeat-description').textContent = '请选择有效的重复方式';
    $('request-preview').textContent = `尚不能发送：${error.message}`;
  }
  updateControls();
}

function chooseAlarm(number) {
  state.drafts.set(state.selected, snapshotForm());
  state.selected = number;
  const form = state.drafts.get(number) || formFromAlarm(state.alarms[number - 1] || defaultAlarm());
  fillForm(form);
  $('editor-title').textContent = `设置第 ${number} 组闹钟`;
  message($('form-message'), state.alarms[number - 1] ? '修改后点击下发，开发板保存确认后才会显示成功。' : '当前为本地草稿；可以先读取开发板，也可以直接下发这一组配置。');
  updateDraft();
  renderCards();
}

function busy(value) {
  state.busy = value;
  updateControls();
}

function showResult({ kind = 'unconfirmed', message: detail, platformCode = null, deviceCode = null, requestId = '' }) {
  const tone = kind === 'confirmed' ? 'success' : kind === 'rejected' ? 'error' : 'warning';
  $('result-panel').dataset.tone = tone;
  $('result-heading').textContent = kind === 'confirmed' ? '开发板已确认保存' : kind === 'rejected' ? '开发板返回保存错误' : '保存结果尚未确认';
  $('result-message').textContent = detail;
  $('result-api').textContent = platformCode === null ? '未取得确认' : String(platformCode);
  $('result-device').textContent = deviceCode === null ? '未取得保存确认' : String(deviceCode);
  $('result-request-id').textContent = requestId || '—';
}

async function callApi(route, payload) {
  if (localFile) throw new Error('请通过本地服务打开页面，直接双击 HTML 无法调用 OneNET。');
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 20000);
  try {
    const response = await fetch(route, {
      method: 'POST', headers: {
        'Content-Type': 'application/json', 'X-Clock-Control': '1',
        Authorization: $('api-token').value.trim(),
      }, body: JSON.stringify(payload), cache: 'no-store', signal: controller.signal,
    });
    let data;
    try { data = await response.json(); } catch { throw new Error('服务未返回有效结果，请读取开发板核对配置。'); }
    if (!response.ok) {
      const localErrors = {
        invalid_authorization: 'API Token 格式不正确，请使用用户级或产品级的 OneNET API Token。',
        upstream_timeout: 'OneNET 响应超时，保存结果尚未确认。请先读取开发板核对，再决定是否重发。',
        result_unconfirmed: '未收到有效的 OneNET 回复，保存结果尚未确认。请先读取开发板核对。',
        rate_limited: '指令发送过快，请至少间隔一秒后再操作。',
        forbidden: '请从本地服务地址打开控制页面。',
        invalid_request: '闹钟参数不符合当前固件要求，请检查各项输入。',
      };
      const explanation = localErrors[data.error] || (typeof data.message === 'string' ? data.message : typeof data.msg === 'string' ? data.msg : `请求失败（HTTP ${response.status}）。`);
      throw new Error(explanation.slice(0, 500));
    }
    return data;
  } catch (error) {
    if (controller.signal.aborted) throw new Error('等待结果超时；指令可能已经到达开发板，请先读取核对，不要连续重发。');
    if (error instanceof TypeError) throw new Error('网络连接中断，结果尚未确认。请检查本地服务，再读取开发板核对。');
    throw error;
  } finally {
    clearTimeout(timeout);
  }
}

async function readBoard(event) {
  event?.preventDefault();
  if (state.busy) return;
  let device;
  try { device = connection(); } catch (error) { message($('connection-message'), error.message, 'error'); return; }
  state.drafts.set(state.selected, snapshotForm());
  busy(true);
  message($('connection-message'), '正在通过 OneNET 向开发板读取当前闹钟，请稍候…');
  try {
    const result = parseReadResponse(await callApi('/api/onenet/read', device));
    state.alarms = result.alarms;
    state.cardNotes.clear();
    // A visited but unedited form is a snapshot, not a user draft. Discard all
    // such snapshots so switching groups uses the newly read board values.
    for (const number of state.drafts.keys()) {
      if (!state.edited.has(number)) state.drafts.delete(number);
    }
    if (!state.edited.has(state.selected)) {
      const alarm = state.alarms[state.selected - 1];
      if (alarm) {
        state.drafts.delete(state.selected);
        fillForm(formFromAlarm(alarm));
      }
    }
    message($('device-state'), '开发板已响应', 'success');
    $('last-read').textContent = `读取于 ${stamp()}`;
    const preserved = state.edited.has(state.selected);
    message($('connection-message'), `已读取 ${result.count} 组闹钟。${preserved ? '当前未下发的草稿已保留。' : ''}${result.invalid.length ? `第 ${result.invalid.join('、')} 组格式异常，未加载。` : ''}`, result.invalid.length ? 'warning' : 'success');
    const actual = state.alarms[state.selected - 1];
    if (actual && preserved) {
      try {
        message($('form-message'), sameAlarm(actual, currentAlarm()) ? '读回配置与当前草稿一致。' : '已更新设备记录；右侧保留你的草稿，尚未下发。', sameAlarm(actual, currentAlarm()) ? 'success' : 'warning');
      } catch { message($('form-message'), '已更新设备记录，右侧保留未完成的草稿。', 'warning'); }
    } else message($('form-message'), actual ? '已载入开发板当前配置。' : '这一组未返回配置；当前表单仍是本地草稿。', actual ? 'success' : 'warning');
  } catch (error) {
    message($('device-state'), '本次读取未成功', 'warning');
    message($('connection-message'), error.message, 'error');
  } finally {
    busy(false);
    updateDraft();
    renderCards();
  }
}

async function sendAlarm(event) {
  event.preventDefault();
  if (state.busy || !$('alarm-form').reportValidity()) return;
  let payload;
  try { payload = buildSetPayload(connection(), state.selected, currentAlarm()); }
  catch (error) { message($('form-message'), error.message, 'error'); return; }
  const number = state.selected;
  state.drafts.set(number, snapshotForm());
  busy(true);
  message($('form-message'), `正在下发第 ${number} 组闹钟，等待开发板保存确认…`);
  try {
    const result = classifySetResponse(await callApi('/api/onenet/set', payload));
    showResult(result);
    if (result.kind === 'confirmed') {
      state.alarms[number - 1] = payload.params[`alarm${number}`];
      state.drafts.delete(number);
      state.edited.delete(number);
      state.cardNotes.set(number, '开发板已确认保存');
      message($('device-state'), '开发板已响应', 'success');
      $('last-read').textContent = `保存确认于 ${stamp()}`;
      message($('form-message'), `第 ${number} 组闹钟已由开发板确认保存。`, 'success');
    } else {
      state.cardNotes.set(number, result.kind === 'rejected' ? '设备返回错误' : '保存结果未确认');
      message($('form-message'), `${result.message}${result.kind === 'unconfirmed' ? ' 请先点击“读取开发板核对”。' : ''}`, result.kind === 'rejected' ? 'error' : 'warning');
    }
  } catch (error) {
    state.cardNotes.set(number, '保存结果未确认');
    showResult({ message: error.message });
    message($('form-message'), `${error.message} 可以读取开发板核对当前配置。`, 'warning');
  } finally {
    busy(false);
    updateDraft();
    renderCards();
  }
}

$('connection-form').addEventListener('submit', readBoard);
$('read-back-button').addEventListener('click', readBoard);
$('alarm-form').addEventListener('submit', sendAlarm);
$('alarm-form').addEventListener('input', () => {
  state.edited.add(state.selected);
  state.cardNotes.delete(state.selected);
  state.drafts.set(state.selected, snapshotForm());
  updateDraft();
  renderCards();
});
document.querySelectorAll('[data-preset]').forEach(button => button.addEventListener('click', () => {
  const mask = { daily: 127, weekdays: 31, weekends: 96 }[button.dataset.preset];
  modes.forEach(input => { input.checked = input.value === 'weekly'; });
  weekdays.forEach(input => { input.checked = Boolean(mask & (1 << Number(input.value))); });
  state.edited.add(state.selected);
  state.drafts.set(state.selected, snapshotForm());
  updateDraft();
  renderCards();
}));
function tokenChanged() {
  const token = validateApiToken($('api-token').value);
  message($('connection-message'), token.valid ? 'API Token 已就绪，可以读取开发板或下发一组闹钟。' : token.reason, token.valid ? 'neutral' : 'warning');
  updateControls();
}
$('api-token').addEventListener('input', tokenChanged);
$('generate-token').addEventListener('click', async () => {
  if (state.busy || generatingToken || localFile) return;
  generatingToken = true;
  updateControls();
  try {
    $('api-token').value = await generateUserApiToken($('token-user-id').value, $('token-access-key').value);
    tokenChanged();
    message($('token-message'), '已在本机生成并填入 API Token，有效期 1 小时。可点击“读取开发板”验证权限。', 'success');
  } catch (error) {
    message($('token-message'), error.message, 'error');
  } finally {
    $('token-access-key').value = '';
    generatingToken = false;
    updateControls();
  }
});
for (const id of ['product-id', 'device-name']) $(id).addEventListener('input', () => {
  state.alarms.fill(null);
  state.drafts.clear();
  state.edited.clear();
  state.cardNotes.clear();
  fillForm(formFromAlarm(defaultAlarm()));
  message($('device-state'), '目标设备已变更', 'neutral');
  message($('connection-message'), '目标设备已变更，请重新读取开发板。');
  message($('form-message'), '当前表单是新目标的本地草稿，尚未下发。');
  $('last-read').textContent = '尚未读取';
  $('result-panel').dataset.tone = 'neutral';
  $('result-heading').textContent = '等待发送指令';
  $('result-message').textContent = '设备目标已变更，请重新读取或下发一组配置。';
  for (const resultId of ['result-api', 'result-device', 'result-request-id']) $(resultId).textContent = '—';
  updateDraft();
  renderCards();
});

if (localFile) {
  $('local-file-hint').hidden = false;
  $('local-file-hint').textContent = '请在工程目录运行 node tools/onenet-alarm-control/server.mjs，再打开 http://127.0.0.1:8787/。';
}
fillForm(formFromAlarm(defaultAlarm()));
updateDraft();
renderCards();
