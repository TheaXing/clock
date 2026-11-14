const $ = id => document.getElementById(id);
let token = '';
let current = null;
let releases = [];
let selected = null;
let activeUpload = null;
let uploadedRelease = null;
let session = 0;

function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) { node.className = className; }
  if (text !== undefined) { node.textContent = text; }
  return node;
}

function size(value) {
  return value >= 1024 * 1024 ? `${(value / 1024 / 1024).toFixed(1)} MiB` : `${(value / 1024).toFixed(value % 1024 ? 1 : 0)} KiB`;
}

function date(value) {
  return new Date(value).toLocaleString('zh-CN', { year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit', hour12: false });
}

function errorMessage(status) {
  return ({ 400: '请求不完整，请重新操作。', 401: '管理令牌无效，请检查后重试。', 403: '请求来源不匹配，请从当前后台地址访问。', 404: '未找到此版本，请刷新列表。', 408: '上传超过 30 秒，请检查连接后重试。', 409: '该版本号已有不同固件，请递增版本后重新构建。', 411: '上传缺少完整文件长度，请重新选择文件。', 413: '文件超过最大 448 KiB 限制。', 415: '请上传未经压缩的 .ota 认证包。', 422: '认证失败：签名、板型、镜像摘要或向量不合法。请检查构建配置与发布密钥。', 429: '已有两份固件正在上传，请稍后重试。', 503: '存储暂不可用或包完整性校验失败，请检查服务器。', 507: '存储已达到配额，现有版本仍予保留。请检查后台存储配置。' })[status] ?? '服务暂不可达，请检查后台进程和局域网连接。';
}

function notice(message, failed = false) {
  $('global-status').textContent = message;
  $('global-status').classList.toggle('error', failed);
  $('global-status').hidden = !message;
}

async function api(path, options = {}) {
  const response = await fetch(path, { ...options, headers: { Authorization: `Bearer ${token}` }, cache: 'no-store' });
  if (!response.ok) { throw new Error(errorMessage(response.status)); }
  return response.json();
}

function render(data) {
  current = data.current;
  releases = data.releases;
  $('current-title').textContent = current ? `v${current.version} 已发布` : '尚未发布固件';
  $('current-description').textContent = current ? '设备下次正常开机联网后，将检查此版本。' : '上传一份认证包，确认发布后即可被设备发现。';
  $('current-version').replaceChildren(document.createTextNode(current ? `v${current.version}` : '—'), element('small', '', current ? 'CURRENT RELEASE' : 'READY WHEN YOU ARE'));
  $('current-meta').replaceChildren();
  $('current-meta').hidden = !current;
  if (current) {
    $('current-meta').append(element('span', '', `${size(current.packageSize)} · ${date(current.publishedAt)} 发布`), element('span', '', `包 ${current.packageId.slice(0, 12)}…`));
  }
  $('release-count').textContent = releases.length;
  $('storage-summary').textContent = `${size(data.storage.usedBytes)} / ${size(data.storage.limitBytes)}`;
  $('empty-state').hidden = releases.length !== 0;
  $('release-list').replaceChildren();
  for (const release of releases) {
    const published = release.packageId === current?.packageId;
    const row = element('article', `release-row${published ? ' current-row' : ''}`);
    const identity = element('div', 'release-identity');
    const symbol = element('span', 'version-symbol', '↓');
    symbol.setAttribute('aria-hidden', 'true');
    const name = element('div');
    name.append(element('strong', 'release-name', `v${release.version}`), element('span', `release-status${published ? ' current' : ''}`, published ? '● 当前发布' : '○ 已上传 · 暂存'));
    identity.append(symbol, name);
    const bytes = element('div', 'release-col release-size');
    bytes.append(element('span', '', '认证包大小'), element('strong', '', size(release.packageSize)));
    const time = element('div', 'release-col release-time');
    time.append(element('span', '', '上传时间'), element('strong', '', date(release.uploadedAt)));
    const action = element('button', 'publish-button', published ? '已发布' : '发布此版本 ↗');
    action.type = 'button';
    action.disabled = published;
    action.setAttribute('aria-label', published ? `v${release.version} 当前已发布` : `发布 v${release.version}`);
    action.addEventListener('click', () => showPublish(release));
    const details = element('details', 'release-details');
    details.append(element('summary', '', '查看包身份与摘要'), element('p', '', `板型：${release.board}`), element('p', '', `ID：${release.packageId}`), element('p', '', `SHA-256：${release.packageSha256}`), element('p', '', `镜像 ${size(release.imageSize)} · 认证包 ${size(release.packageSize)}`));
    row.append(identity, bytes, time, action, details);
    $('release-list').append(row);
  }
  if (uploadedRelease) {
    $('upload-status').textContent = uploadedRelease.packageId === current?.packageId ?
      `v${uploadedRelease.version} 已发布。设备将在下次正常开机联网后检查更新。` :
      `v${uploadedRelease.version} 已上传，认证通过 · ${uploadedRelease.board}。当前为暂存，请在下方确认发布。`;
    $('upload-status').classList.remove('error');
  }
}

async function refresh() {
  const generation = session;
  const data = await api('/api/releases');
  if (generation === session && token) { render(data); }
}

function logout() {
  session++;
  token = '';
  activeUpload?.abort();
  activeUpload = null;
  current = null;
  releases = [];
  selected = null;
  uploadedRelease = null;
  $('token').value = '';
  $('login-status').textContent = '';
  $('workspace').hidden = true;
  $('login-panel').hidden = false;
  $('logout').hidden = true;
  $('publish-dialog').close();
  $('upload-detail').hidden = true;
  $('upload-status').textContent = '上传成功仅代表暂存，请在版本列表中确认发布。';
  $('upload-status').classList.remove('error');
  notice('');
  $('token').focus();
}

$('login-form').addEventListener('submit', async event => {
  event.preventDefault();
  const supplied = $('token').value.trim();
  if (!/^[0-9a-fA-F]{64}$/.test(supplied)) {
    $('login-status').textContent = '请输入 64 位十六进制管理令牌。';
    return;
  }
  token = supplied;
  $('login-submit').disabled = true;
  $('login-status').textContent = '正在验证管理访问…';
  try {
    await refresh();
    $('token').value = '';
    $('login-panel').hidden = true;
    $('workspace').hidden = false;
    $('logout').hidden = false;
    $('login-status').textContent = '';
  } catch (error) {
    token = '';
    $('login-status').textContent = error.message;
  } finally {
    $('login-submit').disabled = false;
  }
});
$('logout').addEventListener('click', logout);
$('refresh').addEventListener('click', async () => {
  $('refresh').disabled = true;
  try { await refresh(); notice('版本列表已刷新。'); } catch (error) { notice(error.message, true); }
  finally { $('refresh').disabled = false; }
});

function showPublish(release) {
  selected = release;
  $('publish-title').textContent = `发布 v${release.version}？`;
  const rollback = current && release.version < current.version;
  $('publish-description').textContent = `${release.board} · ${size(release.packageSize)} · 包 ${release.packageId.slice(0, 12)}…${rollback ? '。这是旧版本回切，仅供人工恢复；自动设备不会降级。' : '。此操作将切换设备可发现的当前发布。'}`;
  $('publish-error').textContent = '';
  $('publish-confirm').textContent = `确认发布 v${release.version}`;
  $('publish-dialog').showModal();
}

$('publish-confirm').addEventListener('click', async () => {
  if (!selected) { return; }
  const release = selected;
  const generation = session;
  $('publish-confirm').disabled = true;
  $('publish-cancel').disabled = true;
  try {
    await api(`/api/releases/${release.packageId}/publish`, { method: 'POST' });
    if (generation !== session) { return; }
    $('publish-dialog').close();
    selected = null;
    if (uploadedRelease?.packageId === release.packageId) {
      $('upload-status').textContent = `v${release.version} 已发布。设备将在下次正常开机联网后检查更新。`;
      $('upload-status').classList.remove('error');
    }
    notice(`v${release.version} 已发布。设备将在下次正常开机联网后检查更新；此状态不代表设备已安装。`);
    await refresh();
  } catch (error) {
    if (generation === session) { $('publish-error').textContent = error.message; }
  } finally {
    $('publish-confirm').disabled = false;
    $('publish-cancel').disabled = false;
  }
});

async function uploadFile(file) {
  if (!file || activeUpload || !token) { return; }
  uploadedRelease = null;
  $('upload-status').classList.remove('error');
  if (!file.name.toLowerCase().endsWith('.ota') || file.size < 1 || file.size > 458752) {
    $('upload-status').textContent = '请选择 .ota 认证包，文件须为 1 至 458752 字节（448 KiB）。';
    $('upload-status').classList.add('error');
    return;
  }
  const generation = session;
  const xhr = new XMLHttpRequest();
  activeUpload = xhr;
  $('drop-zone').classList.add('busy');
  $('package-file').disabled = true;
  $('upload-detail').hidden = false;
  $('upload-name').textContent = file.name;
  $('upload-size').textContent = size(file.size);
  $('upload-progress').value = 0;
  $('upload-status').textContent = '正在上传…';
  try {
    const result = await new Promise((done, reject) => {
      xhr.open('POST', '/api/releases');
      xhr.timeout = 35000;
      xhr.setRequestHeader('Authorization', `Bearer ${token}`);
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      xhr.upload.addEventListener('progress', event => {
        if (!event.lengthComputable || generation !== session) { return; }
        $('upload-progress').value = event.loaded / event.total * 100;
        $('upload-status').textContent = event.loaded === event.total ? '传输完成，正在验证签名与镜像…' : `正在上传 ${Math.round(event.loaded / event.total * 100)}%…`;
      });
      xhr.addEventListener('load', () => {
        if (xhr.status >= 200 && xhr.status < 300) {
          try { done(JSON.parse(xhr.responseText)); } catch { reject(new Error('后台响应不完整，请刷新后检查版本库。')); }
        } else { reject(new Error(errorMessage(xhr.status))); }
      });
      xhr.addEventListener('error', () => reject(new Error(errorMessage(0))));
      xhr.addEventListener('timeout', () => reject(new Error(errorMessage(408))));
      xhr.addEventListener('abort', () => reject(new Error('上传已取消。')));
      xhr.send(file);
    });
    if (generation !== session) { return; }
    uploadedRelease = result.release;
    $('upload-progress').value = 100;
    $('upload-status').textContent = `v${result.release.version} ${result.created ? '已上传，认证通过' : '已存在，无需重复保存'} · ${result.release.board}。尚未发布，请在下方确认。`;
    await refresh();
  } catch (error) {
    if (generation === session) {
      $('upload-status').textContent = error.message;
      $('upload-status').classList.add('error');
    }
  } finally {
    if (activeUpload === xhr) { activeUpload = null; }
    $('drop-zone').classList.remove('busy');
    $('package-file').disabled = false;
    $('package-file').value = '';
  }
}

$('package-file').addEventListener('change', event => uploadFile(event.target.files[0]));
$('drop-zone').addEventListener('keydown', event => {
  if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); $('package-file').click(); }
});
for (const type of ['dragenter', 'dragover']) {
  $('drop-zone').addEventListener(type, event => { event.preventDefault(); $('drop-zone').classList.add('dragging'); });
}
for (const type of ['dragleave', 'drop']) {
  $('drop-zone').addEventListener(type, event => {
    event.preventDefault();
    $('drop-zone').classList.remove('dragging');
    if (type === 'drop') { uploadFile(event.dataTransfer.files[0]); }
  });
}
