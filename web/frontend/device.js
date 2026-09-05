const api = (new URLSearchParams(location.search).get('api') || `${location.protocol}//${location.hostname}:8080`).replace(/\/$/, '');
const $ = s => document.querySelector(s);
const state = {
  banner: {
    root: $('#banner'),
    title: $('#banner-title'),
    detail: $('#banner-detail'),
    meta: $('#banner-meta'),
  },
  logs: $('#action-log'),
  identity: $('#identity-state'),
  network: $('#network-state'),
  health: $('#health-state'),
  alarm: $('#alarm-state'),
  maintenance: $('#maintenance-result'),
  buttons: {
    saveIdentity: $('#save-identity'),
    saveNetwork: $('#save-network'),
    networkTest: $('#network-test'),
    alarmTest: $('#alarm-test'),
    backup: $('#backup'),
    restore: $('#restore'),
    clearLogs: $('#clear-logs'),
    factoryReset: $('#factory-reset'),
  },
};
let bannerTimer = 0;

async function req(path, opt = {}) {
  const response = await fetch(api + path, opt);
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) throw Error(payload.error || `HTTP ${response.status}`);
  return payload;
}

function stamp() {
  return new Date().toLocaleString('zh-CN', { hour12: false });
}

function push(type, title, detail, meta = '') {
  clearTimeout(bannerTimer);
  state.banner.root.dataset.type = type;
  state.banner.title.textContent = title;
  state.banner.detail.textContent = detail;
  state.banner.meta.textContent = meta;
  const line = document.createElement('div');
  line.className = `log-line is-${type}`;
  line.innerHTML = `<strong>${title}</strong><span>${detail}</span><small>${meta || stamp()}</small>`;
  state.logs.prepend(line);
  while (state.logs.children.length > 6) state.logs.removeChild(state.logs.lastElementChild);
  if (type !== 'error') {
    bannerTimer = window.setTimeout(() => {
      state.banner.root.dataset.type = 'info';
      state.banner.title.textContent = '设备管理';
      state.banner.detail.textContent = '可继续执行下一步操作。';
      state.banner.meta.textContent = '';
    }, 5000);
  }
}

function setBusy(button, busy, text) {
  button.disabled = busy;
  button.textContent = busy ? '处理中…' : text;
}

function val(id) {
  return $('#' + id)?.value?.trim() || '';
}

async function load(silent = false) {
  if (!silent) push('info', '正在读取设备信息', '请稍候，页面正在同步设备配置。');
  try {
    const s = await req('/api/device/overview');
    const i = s.identity || {};
    const n = s.network || {};
    const st = s.storage || {};
    $('#device-name').value = i.device_name || '';
    $('#device-id').value = i.device_id || '';
    $('#station-id').value = i.station_id || '';
    $('#device-host').value = i.hostname || '';
    $('#net-mode').value = n.mode || 'dhcp';
    $('#net-interface').value = n.interface || '';
    $('#static-ip').value = n.static_ip || '';
    $('#netmask').value = n.netmask || '';
    $('#gateway').value = n.gateway || '';
    $('#dns').value = n.dns || '';
    $('#wifi-ssid').value = n.wifi_ssid || '';
    $('#net-ip').textContent = n.ip || '--';
    $('#net-mac').textContent = n.mac || '--';
    $('#link-speed').textContent = n.link_speed || '--';
    $('#storage-used').textContent = st.used_gb ?? '--';
    $('#storage-total').textContent = st.total_gb ?? '--';
    $('#service-status').textContent = s.services?.backend || '--';
    $('#alarm-status').textContent = s.peripherals?.alarm_light?.connected ? '已连接' : '未连接';
    $('#device-status').textContent = '● 设备在线';
    $('#device-status').style.color = '#079b61';
    state.health.textContent = `已刷新于 ${stamp()}`;
    if (!silent) push('success', '设备信息已加载', '当前配置已显示在页面中。', `刷新于 ${stamp()}`);
  } catch (error) {
    $('#device-status').textContent = '● 无法连接';
    $('#device-status').style.color = '#d33';
    const message = error instanceof Error ? error.message : '无法连接到设备服务';
    if (!silent) push('error', '读取失败', message);
  }
}

state.buttons.saveIdentity.onclick = async () => {
  const button = state.buttons.saveIdentity;
  const data = {
    device_name: val('device-name'),
    device_id: val('device-id'),
    station_id: val('station-id'),
    hostname: val('device-host'),
  };
  if (!data.device_name || !data.device_id || !data.hostname) {
    push('error', '身份保存失败', '设备名称、设备编号和主机名不能为空');
    return;
  }
  setBusy(button, true, '保存身份');
  push('info', '身份保存中', '正在写入设备身份。');
  try {
    const result = await req('/api/device/identity', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data),
    });
    const identity = result.identity || data;
    $('#device-name').value = identity.device_name;
    $('#device-id').value = identity.device_id;
    $('#station-id').value = identity.station_id;
    $('#device-host').value = identity.hostname;
    state.identity.textContent = `已保存于 ${stamp()}`;
    push('success', '身份已保存', '设备身份配置已更新。', state.identity.textContent);
    localStorage.setItem('device-identity-updated', String(Date.now()));
  } catch (error) {
    const message = error instanceof Error ? error.message : '保存失败';
    state.identity.textContent = `保存失败：${message}`;
    push('error', '身份保存失败', message);
  } finally {
    setBusy(button, false, '保存身份');
  }
};

async function action(path, body = {}, confirmText, successLabel) {
  if (confirmText && !confirm(confirmText)) return;
  try {
    const payload = await req(path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    state.maintenance.textContent = payload.message || `${successLabel}完成`;
    push(payload.ok === false ? 'error' : 'success', successLabel, payload.message || '操作完成', payload.command_hex || stamp());
    await load(true);
  } catch (error) {
    const message = error instanceof Error ? error.message : '操作失败';
    state.maintenance.textContent = `${successLabel}失败：${message}`;
    push('error', `${successLabel}失败`, message);
  }
}

state.buttons.saveNetwork.onclick = async () => {
  const mode = val('net-mode');
  if (mode === 'static' && !val('static-ip')) {
    push('error', '网络保存失败', '静态 IP 模式必须填写 IP 地址');
    return;
  }
  if (!confirm('应用网络配置后 IP 可能变化，浏览器将暂时断开，是否继续？')) return;
  setBusy(state.buttons.saveNetwork, true, '保存并应用网络');
  push('info', '网络保存中', '正在写入网络配置。');
  try {
    await req('/api/network', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        mode,
        interface: val('net-interface'),
        static_ip: val('static-ip'),
        netmask: val('netmask'),
        gateway: val('gateway'),
        dns: val('dns'),
        wifi_ssid: val('wifi-ssid'),
      }),
    });
    state.network.textContent = `已保存于 ${stamp()}`;
    push('success', '网络配置已保存', '配置已写入设备。', state.network.textContent);
    await load(true);
  } catch (error) {
    const message = error instanceof Error ? error.message : '保存失败';
    state.network.textContent = `保存失败：${message}`;
    push('error', '网络配置保存失败', message);
  } finally {
    setBusy(state.buttons.saveNetwork, false, '保存并应用网络');
  }
};

state.buttons.networkTest.onclick = async () => {
  const button = state.buttons.networkTest;
  setBusy(button, true, '网络连通性测试');
  push('info', '网络测试中', '正在执行连通性检查。');
  try {
    const payload = await req('/api/network/test', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    });
    const latency = Number.isFinite(Number(payload.latency_ms)) ? `${payload.latency_ms} ms` : '--';
    const detail = [payload.message || '测试完成', `延时 ${latency}`, payload.network?.ip ? `IP ${payload.network.ip}` : ''].filter(Boolean).join('，');
    state.network.textContent = detail;
    push(payload.ok ? 'success' : 'error', '网络测试完成', detail, stamp());
  } catch (error) {
    const message = error instanceof Error ? error.message : '测试失败';
    state.network.textContent = `测试失败：${message}`;
    push('error', '网络测试失败', message);
  } finally {
    setBusy(button, false, '网络连通性测试');
  }
};

state.buttons.alarmTest.onclick = () => {
  state.buttons.alarmTest.disabled = true;
  state.buttons.alarmTest.textContent = '测试中…';
  push('info', '报警器测试中', '正在发送测试指令。');
  action('/api/peripherals/alarm-light/test', {}, null, '报警器测试').finally(() => {
    state.buttons.alarmTest.disabled = false;
    state.buttons.alarmTest.textContent = '测试报警器';
  });
};

state.buttons.backup.onclick = async () => {
  setBusy(state.buttons.backup, true, '导出配置备份');
  push('info', '备份生成中', '正在导出配置备份。');
  try {
    const payload = await req('/api/config/backup', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    });
    const filename = (payload.file || '').split('/').pop();
    if (!filename) throw Error('后端未返回备份文件名');
    const response = await fetch(`${api}/api/config/backups/${encodeURIComponent(filename)}`);
    if (!response.ok) throw Error('备份下载失败');
    const blob = await response.blob();
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
    state.maintenance.textContent = `备份已下载：${filename}`;
    push('success', '备份已导出', `设备路径 ${payload.file}`, stamp());
  } catch (error) {
    const message = error instanceof Error ? error.message : '导出失败';
    state.maintenance.textContent = `备份失败：${message}`;
    push('error', '备份导出失败', message);
  } finally {
    setBusy(state.buttons.backup, false, '导出配置备份');
  }
};

state.buttons.restore.onclick = () => action('/api/config/restore', {}, '确认恢复最近一次配置备份？', '配置恢复');
state.buttons.clearLogs.onclick = () => action('/api/system/clear-logs', {}, '确认清理缓存和日志？', '日志清理');
state.buttons.factoryReset.onclick = async () => {
  const password = prompt('请输入设备管理员密码（开发环境：admin）');
  if (password !== 'admin') {
    push('error', '恢复出厂取消', '密码错误或已取消。');
    return;
  }
  if (!confirm('恢复出厂设置会清除设备配置，确定继续？')) return;
  await action('/api/system/factory-reset', { confirm: true, password }, null, '恢复出厂');
};

document.querySelectorAll('.tab').forEach(tab => {
  tab.onclick = () => {
    document.querySelectorAll('.tab').forEach(item => item.classList.remove('active'));
    tab.classList.add('active');
    document.body.classList.toggle('wifi-tab', tab.dataset.tab === 'wifi');
  };
});

load();
setInterval(() => load(true), 5000);
