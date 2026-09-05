<template>
  <section class="page model-page">
    <div class="page-title">
      <div>
        <h1>模型管理</h1>
        <p class="muted">YOLOv8 检测模型切换、上传，以及手部模型推理参数配置</p>
      </div>
      <span class="online" :class="{ 'is-error': !online }">● {{ online ? '模型配置已连接' : '连接失败' }}</span>
    </div>

    <section class="settings-status" :class="`is-${banner.type}`" role="status" aria-live="polite">
      <div>
        <strong>{{ banner.title }}</strong>
        <p>{{ banner.detail }}</p>
      </div>
      <span>{{ banner.meta }}</span>
    </section>

    <div class="model-grid">
      <article class="model-card detector-card">
        <div class="model-card-head">
          <div>
            <span class="model-kind">YOLOv8</span>
            <h2>目标检测模型</h2>
            <p>不同 SOP 流程主要切换这一类模型。</p>
          </div>
          <span class="model-state" :class="{ missing: !detector.exists }">{{ detector.exists ? '当前可用' : '文件缺失' }}</span>
        </div>

        <div class="active-model">
          <span>当前模型</span>
          <strong>{{ detector.active_path || '--' }}</strong>
        </div>

        <label>选择已有 YOLO 模型
          <select v-model="selectedDetectorPath">
            <option v-for="file in detector.available_files || files" :key="file.path" :value="file.path">
              {{ file.name }} · {{ file.size_mb }} MB
            </option>
          </select>
        </label>

        <div class="inline-actions">
          <button :disabled="busy.activate || !selectedDetectorPath" @click="activateDetector">
            {{ busy.activate ? '切换中…' : '设为当前模型' }}
          </button>
          <label class="upload-button">
            <input type="file" accept=".rknn" @change="uploadDetector">
            {{ busy.upload ? '上传中…' : '上传 YOLOv8 RKNN' }}
          </label>
        </div>

        <div class="form-grid model-form">
          <label>置信度阈值
            <input type="number" min="0" max="1" step="0.01" v-model.number="values['detector.conf_threshold']">
          </label>
          <label>NMS IoU 阈值
            <input type="number" min="0" max="1" step="0.01" v-model.number="values['detector.iou_threshold']">
          </label>
          <label>输入尺寸
            <input type="number" min="320" max="1280" step="32" v-model.number="values['detector.input_size']">
          </label>
          <label>推理后端
            <input value="rknn" disabled>
          </label>
        </div>
        <label>目标类别
          <textarea v-model="values['detector.labels']" rows="4" placeholder="使用英文逗号分隔，例如 cover_cloth,long_handle"></textarea>
        </label>
      </article>

      <article class="model-card">
        <div class="model-card-head">
          <div>
            <span class="model-kind fixed">固定</span>
            <h2>手部检测模型</h2>
            <p>模型文件固定，只配置检测阈值。</p>
          </div>
          <span class="model-state" :class="{ missing: !hand.exists }">{{ hand.exists ? '当前可用' : '文件缺失' }}</span>
        </div>
        <div class="active-model">
          <span>模型路径</span>
          <strong>{{ hand.active_path || '--' }}</strong>
        </div>
        <div class="form-grid model-form">
          <label>最大手数
            <input type="number" min="1" max="8" step="1" v-model.number="values['hand.max_num_hands']">
          </label>
          <label>检测置信度
            <input type="number" min="0" max="1" step="0.01" v-model.number="values['hand.min_detection_confidence']">
          </label>
          <label>跟踪置信度
            <input type="number" min="0" max="1" step="0.01" v-model.number="values['hand.min_tracking_confidence']">
          </label>
        </div>
      </article>

      <article class="model-card">
        <div class="model-card-head">
          <div>
            <span class="model-kind fixed">固定</span>
            <h2>手部关键点模型</h2>
            <p>关键点模型固定，配置跟踪稳定性参数。</p>
          </div>
          <span class="model-state" :class="{ missing: !landmark.exists }">{{ landmark.exists ? '当前可用' : '文件缺失' }}</span>
        </div>
        <div class="active-model">
          <span>模型路径</span>
          <strong>{{ landmark.active_path || '--' }}</strong>
        </div>
        <div class="form-grid model-form">
          <label>启用约束
            <select v-model="values['hand.constraint.enabled']">
              <option value="true">启用</option>
              <option value="false">关闭</option>
            </select>
          </label>
          <label>标定帧数
            <input type="number" min="1" max="300" step="1" v-model.number="values['hand.constraint.calibration_frames']">
          </label>
          <label>平滑系数
            <input type="number" min="0" max="1" step="0.01" v-model.number="values['hand.constraint.smoothing']">
          </label>
          <label>最大预测帧
            <input type="number" min="0" max="120" step="1" v-model.number="values['hand.constraint.max_prediction_frames']">
          </label>
        </div>
      </article>
    </div>

    <article class="model-card config-actions-card">
      <div>
        <h2>配置发布</h2>
        <p>保存后写入 `config/sop_config.txt`。如果算法正在运行，请重启算法后让新模型和阈值生效。</p>
      </div>
      <button :disabled="busy.save || !online" @click="saveConfig">{{ busy.save ? '保存中…' : '保存模型配置' }}</button>
    </article>
  </section>
</template>

<script setup>
import { computed, onMounted, reactive, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { api, post, put, request } from '../api';

const modelData = ref({ models: [], files: [] });
const values = reactive({});
const selectedDetectorPath = ref('');
const online = ref(false);
const busy = reactive({ save: false, upload: false, activate: false });
const banner = ref({ type: 'info', title: '正在读取模型配置', detail: '正在扫描 RKNN 模型文件和推理参数。', meta: '' });

const files = computed(() => modelData.value.files || []);
const detector = computed(() => findModel('detector'));
const hand = computed(() => findModel('hand'));
const landmark = computed(() => findModel('landmark'));

function findModel(kind) {
  return modelData.value.models?.find(item => item.kind === kind) || { config: {}, available_files: [] };
}

function stamp() {
  return new Date().toLocaleString('zh-CN', { hour12: false });
}

function setBanner(type, title, detail, meta = '') {
  banner.value = { type, title, detail, meta };
  if (type !== 'info') ElMessage({ type, message: `${title}：${detail}` });
}

function applyModelData(data) {
  modelData.value = data;
  Object.keys(values).forEach(key => delete values[key]);
  for (const model of data.models || []) Object.assign(values, model.config || {});
  selectedDetectorPath.value = detector.value.active_path || files.value[0]?.path || '';
}

async function load() {
  setBanner('info', '正在读取模型配置', '正在扫描 RKNN 模型文件和推理参数。');
  try {
    applyModelData(await request('/api/models'));
    online.value = true;
    setBanner('success', '模型配置已加载', '当前模型和阈值已显示在页面中。', `刷新于 ${stamp()}`);
  } catch (error) {
    online.value = false;
    setBanner('error', '读取失败', error instanceof Error ? error.message : '无法连接模型接口');
  }
}

function validateConfig() {
  const numericRanges = [
    ['detector.conf_threshold', 0, 1],
    ['detector.iou_threshold', 0, 1],
    ['hand.min_detection_confidence', 0, 1],
    ['hand.min_tracking_confidence', 0, 1],
    ['hand.constraint.smoothing', 0, 1],
    ['detector.input_size', 320, 1280],
    ['hand.max_num_hands', 1, 8],
    ['hand.constraint.calibration_frames', 1, 300],
    ['hand.constraint.max_prediction_frames', 0, 120],
  ];
  for (const [key, min, max] of numericRanges) {
    const value = Number(values[key]);
    if (!Number.isFinite(value) || value < min || value > max) throw new Error(`${key} 必须在 ${min} 到 ${max} 之间`);
  }
  if (!String(values['detector.labels'] || '').split(',').some(item => item.trim())) throw new Error('目标类别不能为空');
}

async function saveConfig() {
  busy.save = true;
  try {
    validateConfig();
    applyModelData(await put('/api/models/config', { values }));
    setBanner('success', '模型配置已保存', '阈值和当前模型已写入 sop_config.txt。', `完成于 ${stamp()}`);
  } catch (error) {
    setBanner('error', '保存失败', error instanceof Error ? error.message : '模型配置保存失败');
  } finally {
    busy.save = false;
  }
}

async function activateDetector() {
  busy.activate = true;
  try {
    applyModelData(await post('/api/models/detector/activate', { path: selectedDetectorPath.value }));
    setBanner('success', 'YOLO 模型已切换', selectedDetectorPath.value, `完成于 ${stamp()}`);
  } catch (error) {
    setBanner('error', '模型切换失败', error instanceof Error ? error.message : '无法切换模型');
  } finally {
    busy.activate = false;
  }
}

async function uploadDetector(event) {
  const file = event.target.files?.[0];
  event.target.value = '';
  if (!file) return;
  busy.upload = true;
  try {
    const response = await fetch(`${api}/api/models/detector/upload?filename=${encodeURIComponent(file.name)}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: await file.arrayBuffer(),
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || data.detail || data.message || `上传失败（HTTP ${response.status}）`);
    applyModelData(data.activated);
    setBanner('success', 'YOLO 模型已上传并启用', data.file?.path || file.name, `完成于 ${stamp()}`);
  } catch (error) {
    setBanner('error', '上传失败', error instanceof Error ? error.message : '模型上传失败');
  } finally {
    busy.upload = false;
  }
}

onMounted(load);
</script>
