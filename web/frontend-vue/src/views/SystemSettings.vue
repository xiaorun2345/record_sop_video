<template>
  <section class="page settings-page">
    <div class="page-title">
      <div>
        <h1>系统设置</h1>
        <p class="muted">时间、存储、服务和日志策略</p>
      </div>
      <button class="ghost" :disabled="loading" @click="loadSettings">{{ loading ? '读取中…' : '重新读取' }}</button>
    </div>

    <section class="settings-status" :class="`is-${banner.type}`" role="status" aria-live="polite">
      <div>
        <strong>{{ banner.title }}</strong>
        <p>{{ banner.detail }}</p>
      </div>
      <span>{{ banner.meta }}</span>
    </section>

    <div class="cards" :aria-busy="loading">
      <article>
        <h2>时间与区域</h2>
        <label>时区<select v-model="time.timezone"><option>UTC</option><option>Asia/Shanghai</option></select></label>
        <label>NTP 开关<input type="checkbox" v-model="time.ntp_enabled"></label>
        <label>NTP 服务器<input v-model="time.ntp_server" :disabled="!time.ntp_enabled"></label>
        <button :disabled="loading || !loaded.time || saving.time" @click="save('time','/api/time',time,'时间设置')">
          {{ saving.time ? '保存中…' : '保存时间设置' }}
        </button>
        <p class="section-hint" role="status" aria-live="polite">{{ sectionStates.time }}</p>
      </article>

      <article>
        <h2>存储策略</h2>
        <label>自动清理天数<input type="number" min="1" max="3650" v-model.number="storage.auto_cleanup_days"></label>
        <label>保留视频<input type="checkbox" v-model="storage.video_retention_enabled"></label>
        <label>低空间阈值 %<input type="number" min="1" max="90" v-model.number="storage.low_space_threshold_percent"></label>
        <button :disabled="loading || !loaded.storage || saving.storage" @click="save('storage','/api/storage-policy',storage,'存储策略')">
          {{ saving.storage ? '保存中…' : '保存存储策略' }}
        </button>
        <p class="section-hint" role="status" aria-live="polite">{{ sectionStates.storage }}</p>
      </article>

      <article>
        <h2>服务配置</h2>
        <label>API 端口<input type="number" v-model.number="services.api_port"></label>
        <label>Node-RED 端口<input type="number" v-model.number="services.node_red_port"></label>
        <button :disabled="loading || !loaded.services || saving.services" @click="save('services','/api/services/config',services,'服务配置')">
          {{ saving.services ? '保存中…' : '保存服务配置' }}
        </button>
        <p class="hint">此处保存端口配置，不会自动重启服务或切换当前访问地址。</p>
        <p class="section-hint" role="status" aria-live="polite">{{ sectionStates.services }}</p>
      </article>

      <article>
        <h2>日志设置</h2>
        <p class="hint">当前版本尚未提供日志级别和保留天数的保存接口。</p>
        <RouterLink class="settings-link" to="/device">前往设备管理清理日志 →</RouterLink>
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
import { onMounted, reactive, ref } from 'vue';
import { request, put } from '../api';
import { ElMessage } from 'element-plus';

const time = ref({});
const storage = ref({});
const services = ref({});
const saving = reactive({ time: false, storage: false, services: false });
const loading = ref(false);
const loaded = reactive({ time: false, storage: false, services: false });
const banner = ref({
  type: 'info',
  title: '正在读取系统设置',
  detail: '请稍候，页面正在同步设备配置。',
  meta: '',
});
const sectionStates = reactive({
  time: '尚未保存',
  storage: '尚未保存',
  services: '尚未保存',
});
const log = ref([]);

function stamp() {
  return new Date().toLocaleString('zh-CN', { hour12: false });
}

function setBanner(type, title, detail, meta = '') {
  banner.value = { type, title, detail, meta };
  if (type === 'error' || title.endsWith('已保存')) ElMessage({ type, message: `${title}：${detail}` });
}

function pushLog(type, title, detail, meta = '') {
  log.value = [{ id: Date.now() + Math.random(), type, title, detail, meta }, ...log.value].slice(0, 6);
}

async function loadSettings() {
  setBanner('info', '正在读取系统设置', '请稍候，页面正在同步设备配置。');
  pushLog('info', '正在读取系统设置', '请稍候，页面正在同步设备配置。');
  loading.value = true;
  const sections = [['time', '/api/time', time], ['storage', '/api/storage-policy', storage], ['services', '/api/services/config', services]];
  await Promise.all(sections.map(async ([key, path, target]) => {
    try {
      target.value = await request(path);
      loaded[key] = true;
      sectionStates[key] = '已读取设备配置';
    } catch (error) {
      loaded[key] = false;
      sectionStates[key] = `读取失败：${error.message}`;
    }
  }));
  loading.value = false;
  const success = Object.values(loaded).every(Boolean);
  setBanner(success ? 'success' : 'error', success ? '系统设置已加载' : '部分配置读取失败', success ? '修改后请保存对应分组。' : '请查看分组内的错误提示，点击重新读取重试。', stamp());
}

async function save(key, path, data, label) {
  if (saving[key]) return;
  const ranges = { storage: [['auto_cleanup_days', 1, 3650], ['low_space_threshold_percent', 1, 90]], services: [['api_port', 1024, 65535], ['node_red_port', 1024, 65535]] };
  if ((ranges[key] || []).some(([field, min, max]) => !Number.isInteger(data[field]) || data[field] < min || data[field] > max)) {
    sectionStates[key] = key === 'storage' ? '请输入有效整数：清理天数 1–3650，空间阈值 1–90。' : '端口必须为 1024–65535 之间的整数。';
    setBanner('error', '请检查输入', sectionStates[key]);
    return;
  }
  saving[key] = true;
  setBanner('info', `${label}保存中`, '正在写入设备配置。');
  sectionStates[key] = `${label}保存中…`;
  pushLog('info', `${label}保存中`, '正在写入设备配置。');
  try {
    await put(path, data);
    const now = stamp();
    setBanner('success', `${label}已保存`, '配置已写入设备。', now);
    sectionStates[key] = `已保存于 ${now}`;
    pushLog('success', `${label}已保存`, '配置已写入设备。', `完成于 ${now}`);
  } catch (error) {
    const message = error instanceof Error ? error.message : '保存失败';
    sectionStates[key] = `保存失败：${message}`;
    setBanner('error', `${label}保存失败`, message);
    pushLog('error', `${label}保存失败`, message);
  } finally {
    saving[key] = false;
  }
}

onMounted(loadSettings);
</script>
