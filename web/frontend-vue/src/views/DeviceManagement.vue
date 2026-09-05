<template>
  <section class="page device-page">
    <div class="page-title">
      <div>
        <h1>设备管理</h1>
        <p class="muted">设备身份、网络、健康状态和维护操作</p>
      </div>
      <span class="online" :class="{ 'is-error': !online }">● {{ online ? '设备在线' : '连接失败' }}</span>
    </div>

    <section class="settings-status" :class="`is-${banner.type}`" role="status" aria-live="polite">
      <div>
        <strong>{{ banner.title }}</strong>
        <p>{{ banner.detail }}</p>
      </div>
      <span>{{ banner.meta }}</span>
    </section>

    <div class="device-top-grid">
      <article>
        <h2>设备身份</h2>
        <label>设备名称<input v-model="form.device_name"></label>
        <label>设备编号<input v-model="form.device_id"></label>
        <label>工位编号<input v-model="form.station_id"></label>
        <label>主机名<input v-model="form.hostname"></label>
        <button :disabled="!online || busy.identity" @click="saveIdentity">{{ busy.identity ? '保存中…' : '保存身份' }}</button>
        <p class="section-hint" role="status" aria-live="polite">{{ sectionState.identity }}</p>
      </article>

      <article class="maintenance-card">
        <h2>维护操作</h2>
        <div class="maintenance-actions">
          <button :disabled="busy.backup" @click="backup">{{ busy.backup ? '生成中…' : '导出配置备份' }}</button>
          <button :disabled="busy.restore" @click="restore">{{ busy.restore ? '恢复中…' : '恢复最近备份' }}</button>
          <button :disabled="busy.clearLogs" @click="clearLogs">{{ busy.clearLogs ? '清理中…' : '清理缓存和日志' }}</button>
          <button class="danger" disabled title="后端尚未实现恢复出厂操作">恢复出厂设置（暂未开放）</button>
        </div>
        <p class="hint">{{ sectionState.maintenance || '涉及系统数据的操作会进行二次确认。' }}</p>
      </article>
    </div>

    <div class="cards">
      <article class="wide">
        <h2>网络配置</h2><p class="hint">保存有线 / Wi-Fi 参数；切换标签仅显示对应字段，不会切换设备网络。配置需由管理员应用。</p>
        <div class="tabs">
          <button :class="{ active: tab === 'ethernet' }" @click="tab = 'ethernet'">有线网络</button>
          <button :class="{ active: tab === 'wifi' }" @click="tab = 'wifi'">Wi‑Fi</button>
        </div>
        <div class="form-grid">
          <label>地址模式<select v-model="network.mode"><option value="dhcp">DHCP 自动获取</option><option value="static">静态 IP</option></select></label>
          <label>网卡<input v-model="network.interface"></label>
          <label>静态 IP<input v-model="network.static_ip" :disabled="network.mode !== 'static'"></label>
          <label>子网掩码<input v-model="network.netmask" :disabled="network.mode !== 'static'"></label>
          <label>网关<input v-model="network.gateway" :disabled="network.mode !== 'static'"></label>
          <label>DNS<input v-model="network.dns"></label>
          <label v-if="tab === 'wifi'">Wi‑Fi SSID<input v-model="network.wifi_ssid"></label>
          <label v-if="tab === 'wifi'">Wi‑Fi 密码<input v-model="network.wifi_password" type="password"></label>
        </div>
        <p class="hint">当前 IP：{{ network.ip || '--' }}　MAC：{{ network.mac || '--' }}　链路：{{ network.link_speed || '--' }}</p>
        <div class="inline-actions">
          <button :disabled="!online || busy.network" @click="saveNetwork">{{ busy.network ? '保存中…' : '保存网络配置' }}</button>
          <button class="ghost" :disabled="busy.networkTest" @click="testNetwork">{{ busy.networkTest ? '测试中…' : '网络连通性测试' }}</button>
        </div>
        <p class="section-hint" role="status" aria-live="polite">{{ sectionState.network }}</p>
      </article>

      <article class="wide health-card" :class="`storage-${storageStatus}`">
        <div class="health-heading">
          <div>
            <h2>设备健康</h2>
            <p class="hint">CPU、内存、存储、温度、服务和硬件外设状态</p>
          </div>
          <span class="health-badge" :class="`is-${overallStatus}`">{{ overallStatusText }}</span>
        </div>

        <div class="health-metrics">
          <div v-for="metric in healthMetrics" :key="metric.label" class="health-metric" :class="`is-${metric.status}`">
            <span>{{ metric.label }}</span>
            <strong>{{ metric.value }}</strong>
            <small>{{ metric.detail }}</small>
          </div>
        </div>

        <div class="health-chart-grid">
          <div class="health-chart-panel">
            <div class="chart-title">
              <strong>资源占用</strong>
              <small>实时硬件负载</small>
            </div>
            <div v-for="item in usageCharts" :key="item.label" class="usage-row" :class="`is-${item.status}`">
              <div class="usage-label">
                <span>{{ item.label }}</span>
                <strong>{{ item.value }}%</strong>
              </div>
              <div class="usage-bar" role="meter" :aria-valuenow="item.value" aria-valuemin="0" aria-valuemax="100">
                <i :style="{ width: `${item.value}%` }"></i>
              </div>
              <small>{{ item.detail }}</small>
            </div>
          </div>

          <div class="health-chart-panel status-panel">
            <div class="chart-title">
              <strong>硬件状态</strong>
              <small>服务、网络、外设</small>
            </div>
            <div class="health-status-list">
              <div v-for="item in hardwareStatus" :key="item.label" class="hardware-status" :class="`is-${item.status}`">
                <span>{{ item.label }}</span>
                <strong>{{ item.value }}</strong>
              </div>
            </div>
          </div>
        </div>
        <p class="section-hint" role="status" aria-live="polite">{{ sectionState.health }}</p>
      </article>

      <article>
        <h2>声光报警器</h2>
        <p>状态：<strong>{{ overview.peripherals?.alarm_light?.connected ? '已连接' : '未连接' }}</strong></p>
        <p>路径：<code>/dev/ch341-light</code></p>
        <p>波特率：<strong>9600（固定）</strong></p>
        <button :disabled="busy.alarmTest" @click="alarmTest">{{ busy.alarmTest ? '测试中…' : '测试报警器' }}</button>
        <p class="section-hint" role="status" aria-live="polite">{{ sectionState.alarm }}</p>
      </article>

      <article class="wide">
        <h2>最近操作</h2>
        <div class="action-log">
          <div v-for="item in log" :key="item.id" :class="['log-line', `is-${item.type}`]">
            <strong>{{ item.title }}</strong>
            <span>{{ item.detail }}</span>
            <small>{{ item.meta }}</small>
          </div>
          <p v-if="!log.length" class="hint">暂无操作记录</p>
        </div>
      </article>
    </div>
  </section>
</template>

<script setup>
import { computed, onMounted, reactive, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { useDeviceStore } from '../stores/device';
const deviceStore = useDeviceStore();
import { api, request, put, post } from '../api';

const form = ref({});
const network = ref({});
const overview = ref({});
const health = ref({});
const online = ref(false);
const tab = ref('ethernet');
const busy = reactive({
  identity: false,
  network: false,
  networkTest: false,
  alarmTest: false,
  backup: false,
  restore: false,
  clearLogs: false,
  factoryReset: false,
});
const banner = ref({
  type: 'info',
  title: '正在读取设备信息',
  detail: '请稍候，页面正在同步设备配置。',
  meta: '',
});
const sectionState = reactive({
  identity: '尚未保存',
  network: '尚未保存',
  maintenance: '',
  alarm: '',
  health: '',
});
const log = ref([]);

function numberOrZero(value) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? numeric : 0;
}

function percent(value) {
  return Math.max(0, Math.min(100, Math.round(numberOrZero(value))));
}

function fixed(value, digits = 1) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? numeric.toFixed(digits) : '--';
}

function gb(value) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? `${numeric.toFixed(1)} GB` : '--';
}

function usageStatus(value) {
  const used = numberOrZero(value);
  if (used >= 90) return 'critical';
  if (used >= 75) return 'warning';
  return 'ok';
}

function temperatureStatus(value) {
  const temp = numberOrZero(value);
  if (temp >= 80) return 'critical';
  if (temp >= 70) return 'warning';
  return 'ok';
}

const storage = computed(() => health.value.storage || overview.value.storage || {});
const storageStatus = computed(() => storage.value.status || usageStatus(storage.value.used_percent));
const cpuStatus = computed(() => temperatureStatus(health.value.cpu?.temperature_c));
const overallStatus = computed(() => {
  if (storageStatus.value === 'critical' || cpuStatus.value === 'critical') return 'critical';
  if (storageStatus.value === 'warning' || cpuStatus.value === 'warning') return 'warning';
  return online.value ? 'ok' : 'critical';
});
const overallStatusText = computed(() => {
  if (overallStatus.value === 'critical') return '需要处理';
  if (overallStatus.value === 'warning') return '注意观察';
  return '运行正常';
});
const healthMetrics = computed(() => {
  const cpu = health.value.cpu || {};
  const memory = health.value.memory || {};
  return [
    {
      label: 'CPU 温度',
      value: `${fixed(cpu.temperature_c, 1)} °C`,
      detail: `${cpu.cores || '--'} 核 · 负载 ${fixed(cpu.load1, 2)}`,
      status: temperatureStatus(cpu.temperature_c),
    },
    {
      label: '内存使用',
      value: `${fixed(memory.used_percent, 1)}%`,
      detail: `${gb(memory.used_gb)} / ${gb(memory.total_gb)}`,
      status: usageStatus(memory.used_percent),
    },
    {
      label: '存储剩余',
      value: `${fixed(storage.value.free_percent, 1)}%`,
      detail: `${gb(storage.value.free_gb)} 可用`,
      status: storageStatus.value,
    },
    {
      label: '运行时间',
      value: uptimeText.value,
      detail: health.value.summary?.updated_at || '--',
      status: 'ok',
    },
  ];
});
const usageCharts = computed(() => {
  const cpu = health.value.cpu || {};
  const memory = health.value.memory || {};
  return [
    {
      label: 'CPU 负载',
      value: percent(cpu.load_percent),
      detail: `${cpu.load1 ?? '--'} / ${cpu.cores || '--'} 核`,
      status: usageStatus(cpu.load_percent),
    },
    {
      label: '内存',
      value: percent(memory.used_percent),
      detail: `${gb(memory.used_gb)} 已用`,
      status: usageStatus(memory.used_percent),
    },
    {
      label: '磁盘',
      value: percent(storage.value.used_percent),
      detail: `${gb(storage.value.used_gb)} / ${gb(storage.value.total_gb)}，剩余 ${fixed(storage.value.free_percent, 1)}%`,
      status: storageStatus.value,
    },
  ];
});
const hardwareStatus = computed(() => {
  const network = health.value.network || overview.value.network || {};
  const services = health.value.services || overview.value.services || {};
  const alarm = health.value.peripherals?.alarm_light || overview.value.peripherals?.alarm_light || {};
  return [
    { label: '后端服务', value: services.backend || '--', status: services.backend ? 'ok' : 'warning' },
    { label: 'MediaMTX', value: services.mediamtx || '--', status: 'ok' },
    { label: '网络', value: network.status || '--', status: network.status === '已连接' ? 'ok' : 'warning' },
    { label: '声光报警器', value: alarm.connected ? '已连接' : '未连接', status: alarm.connected ? 'ok' : 'warning' },
  ];
});
const uptimeText = computed(() => {
  const seconds = numberOrZero(health.value.summary?.uptime_seconds);
  if (!seconds) return '--';
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  if (days) return `${days} 天 ${hours} 小时`;
  return `${hours} 小时 ${Math.floor((seconds % 3600) / 60)} 分钟`;
});

function stamp() {
  return new Date().toLocaleString('zh-CN', { hour12: false });
}

function pushLog(type, title, detail, meta = '') {
  banner.value = { type, title, detail, meta };
  if (type === 'error' || (type === 'success' && title !== '设备信息已加载')) ElMessage({ type, message: `${title}：${detail}` });
  log.value = [{ id: Date.now() + Math.random(), type, title, detail, meta }, ...log.value].slice(0, 6);
}

async function load() {
  pushLog('info', '正在读取设备信息', '请稍候，页面正在同步设备配置。');
  try {
    overview.value = await request('/api/device/overview');
    form.value = { ...overview.value.identity };
    network.value = { ...overview.value.network };
    health.value = await request('/api/device/health');
    online.value = true;
    sectionState.identity = '已读取设备配置';
    sectionState.network = '已读取网络配置';
    sectionState.health = `已刷新于 ${stamp()}`;
    pushLog('success', '设备信息已加载', '当前配置已显示在页面中。', `刷新于 ${stamp()}`);
  } catch (error) {
    online.value = false;
    const message = error instanceof Error ? error.message : '无法连接到设备服务';
    pushLog('error', '读取失败', message);
  }
}

async function saveIdentity() {
  busy.identity = true;
  pushLog('info', '身份保存中', '正在写入设备身份。');
  try {
    const result = await put('/api/device/identity', form.value);
    form.value = { ...(result.identity || form.value) };
    deviceStore.identity = { ...form.value };
    sectionState.identity = `已保存于 ${stamp()}`;
    pushLog('success', '身份已保存', '设备身份配置已更新。', sectionState.identity);
  } catch (error) {
    const message = error instanceof Error ? error.message : '保存失败';
    sectionState.identity = `保存失败：${message}`;
    pushLog('error', '身份保存失败', message);
  } finally {
    busy.identity = false;
  }
}

async function saveNetwork() {
  busy.network = true;
  pushLog('info', '网络保存中', '正在写入网络配置。');
  try {
    await put('/api/network', network.value);
    sectionState.network = `已保存于 ${stamp()}`;
    pushLog('success', '网络配置已保存', '配置已保存；当前版本不会自动应用到系统网络。', sectionState.network);
  } catch (error) {
    const message = error instanceof Error ? error.message : '保存失败';
    sectionState.network = `保存失败：${message}`;
    pushLog('error', '网络配置保存失败', message);
  } finally {
    busy.network = false;
  }
}

async function testNetwork() {
  busy.networkTest = true;
  pushLog('info', '网络测试中', '正在执行连通性检查。');
  try {
    const result = await post('/api/network/test');
    const meta = Number.isFinite(Number(result.latency_ms)) ? `${result.latency_ms} ms` : '';
    const detail = [result.message, meta ? `延时 ${meta}` : '', result.network?.ip ? `IP ${result.network.ip}` : ''].filter(Boolean).join('，');
    sectionState.network = detail;
    pushLog(result.ok ? 'success' : 'error', '网络测试完成', detail || '测试结束', `时间 ${stamp()}`);
  } catch (error) {
    const message = error instanceof Error ? error.message : '测试失败';
    sectionState.network = `测试失败：${message}`;
    pushLog('error', '网络测试失败', message);
  } finally {
    busy.networkTest = false;
  }
}

async function alarmTest() {
  busy.alarmTest = true;
  pushLog('info', '报警器测试中', '正在发送测试指令。');
  try {
    const result = await post('/api/peripherals/alarm-light/test');
    sectionState.alarm = result.message || '报警器测试完成';
    pushLog(result.ok === false ? 'error' : 'success', '报警器测试完成', sectionState.alarm, result.command_hex || '');
  } catch (error) {
    const message = error instanceof Error ? error.message : '测试失败';
    sectionState.alarm = `测试失败：${message}`;
    pushLog('error', '报警器测试失败', message);
  } finally {
    busy.alarmTest = false;
  }
}

async function backup() {
  busy.backup = true;
  pushLog('info', '备份生成中', '正在导出配置备份。');
  try {
    const result = await post('/api/config/backup');
    const filename = (result.file || '').split('/').pop();
    if (!filename) throw new Error('后端未返回备份文件名');
    const response = await fetch(`${api}/api/config/backups/${encodeURIComponent(filename)}`);
    if (!response.ok) throw new Error('备份下载失败');
    const blob = await response.blob();
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    link.remove();
    window.setTimeout(() => URL.revokeObjectURL(link.href), 1000);
    sectionState.maintenance = `备份已导出：${filename}`;
    pushLog('success', '备份已导出', `设备路径 ${result.file}`, `完成于 ${stamp()}`);
  } catch (error) {
    const message = error instanceof Error ? error.message : '导出失败';
    sectionState.maintenance = `备份失败：${message}`;
    pushLog('error', '备份导出失败', message);
  } finally {
    busy.backup = false;
  }
}

async function restore() {
  if (!window.confirm('确认恢复最近备份？')) return;
  busy.restore = true;
  pushLog('info', '恢复备份中', '正在恢复最近配置。');
  try {
    const result = await post('/api/config/restore');
    sectionState.maintenance = result.message || '恢复完成';
    pushLog('success', '配置恢复完成', sectionState.maintenance, result.file || '');
    await load();
  } catch (error) {
    const message = error instanceof Error ? error.message : '恢复失败';
    sectionState.maintenance = `恢复失败：${message}`;
    pushLog('error', '恢复失败', message);
  } finally {
    busy.restore = false;
  }
}

async function clearLogs() {
  if (!window.confirm('确认清理缓存和日志？')) return;
  busy.clearLogs = true;
  pushLog('info', '清理日志中', '正在清理运行日志。');
  try {
    const result = await post('/api/system/clear-logs');
    sectionState.maintenance = result.message || '日志清理完成';
    pushLog('success', '日志清理完成', sectionState.maintenance, `清理 ${result.removed ?? 0} 个文件`);
  } catch (error) {
    const message = error instanceof Error ? error.message : '清理失败';
    sectionState.maintenance = `清理失败：${message}`;
    pushLog('error', '日志清理失败', message);
  } finally {
    busy.clearLogs = false;
  }
}

async function factoryReset() {
  const password = window.prompt('请输入设备管理员密码（开发环境：admin）');
  if (password !== 'admin') {
    pushLog('error', '恢复出厂取消', '密码错误或已取消。');
    return;
  }
  if (!window.confirm('恢复出厂设置会清除设备配置，确定继续？')) return;
  busy.factoryReset = true;
  pushLog('info', '恢复出厂中', '正在登记恢复出厂请求。');
  try {
    const result = await post('/api/system/factory-reset', { confirm: true, password });
    sectionState.maintenance = result.message || '恢复出厂请求已登记';
    pushLog('success', '恢复出厂请求已登记', sectionState.maintenance, `时间 ${stamp()}`);
  } catch (error) {
    const message = error instanceof Error ? error.message : '操作失败';
    sectionState.maintenance = `恢复失败：${message}`;
    pushLog('error', '恢复出厂失败', message);
  } finally {
    busy.factoryReset = false;
  }
}

onMounted(load);
</script>
