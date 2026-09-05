<template>
  <section class="page workbench">
    <div class="page-title">
      <div><h1>实时作业工作台</h1><p class="muted">摄像头、算法检测与作业状态</p></div>
    </div>
    <div class="work-grid">
      <div>
        <div class="video-card">
          <div class="video-stage">
            <video ref="video" autoplay muted playsinline></video>
            <span class="fps">FPS {{ status.fps?.toFixed?.(1) || "--" }}</span>
            <span class="stream-tag">{{ status.algorithm === "running" ? "算法结果" : "原始视频" }}</span>
            <span v-if="streamMessage" class="stream-message">{{ streamMessage }}</span>
          </div>
          <div class="actions">
            <button @click="camera('start')" :disabled="busy || status.camera === 'running'">启动摄像头</button>
            <button @click="camera('stop')" :disabled="busy || status.camera !== 'running'">关闭摄像头</button>
            <button @click="algorithm('start')" :disabled="busy || status.camera !== 'running' || status.algorithm === 'running'">启动算法</button>
            <button @click="algorithm('stop')" :disabled="busy || status.algorithm !== 'running'">关闭算法</button>
            <button class="record" @click="recording" :disabled="busy">{{ recordingOn ? "停止录制" : "开始录制" }}</button>
            <button class="ghost compact-action export-action" @click="history = true; loadVideos()">视频导出</button>
          </div>
        </div>
        <div class="metric-row">
          <Metric label="FPS" :value="status.fps" unit="FPS" />
          <Metric label="NPU 延迟" :value="status.npu_latency_ms" unit="ms" />
          <Metric label="CPU 温度" :value="status.cpu_temperature_c" unit="°C" />
        </div>
      </div>
      <aside class="job-card">
        <h2>当前 SOP 步骤</h2>
        <template v-if="sopRuntime.active && currentLiveStep">
          <div class="sop-runtime-head"><div><div class="step-current">{{ currentLiveStep.name }}</div><p>{{ currentLiveStep.index + 1 }} / {{ liveSteps.length }} · {{ stateLabel(currentLiveStep.state) }}</p></div><span class="runtime-chip" :class="`is-${currentLiveStep.state}`">{{ stateLabel(currentLiveStep.state) }}</span></div>
          <div class="progress"><i :style="{ width: `${progress}%` }"></i></div>
          <div class="runtime-meta"><span>步骤耗时 {{ Number(currentLiveStep.elapsedSec || 0).toFixed(1) }}s</span><span>确认 {{ currentLiveStep.confirmCount }} / {{ currentLiveStep.confirmTarget }} 帧</span></div>
          <div class="runtime-checks">
            <div v-for="item in currentLiveStep.objects" :key="item.id || item.label" class="runtime-check" :class="{ ok: item.satisfiedNow }">
              <span>{{ item.satisfiedNow ? '✓' : '✕' }}</span><strong>{{ item.label }}</strong><em>{{ item.currentCount }} / {{ item.requiredCount }}</em>
              <small>{{ item.reason }}</small>
            </div>
            <div v-if="currentLiveStep.handRoiConfigured" class="runtime-check" :class="{ ok: currentLiveStep.handRoiSatisfied }"><span>{{ currentLiveStep.handRoiSatisfied ? '✓' : '✕' }}</span><strong>手部作业区域</strong><em>{{ currentLiveStep.handRoiSatisfied ? '已进入' : '未进入' }}</em></div>
            <div v-if="!currentLiveStep.spatialSatisfied" class="runtime-check"><span>✕</span><strong>手物空间距离</strong><em>未满足</em></div>
            <div v-if="!currentLiveStep.objects?.length" class="runtime-empty">当前步骤没有可判断的必检对象</div>
          </div>
          <div v-if="runtimeState.alerts?.length" class="runtime-alert">{{ runtimeState.alerts[0].message }}</div>
        </template>
        <template v-else-if="sopRuntime.active && runtimeState.finished"><div class="step-current">SOP 已全部完成</div><p>全部启用步骤均已满足判断条件。</p></template>
        <template v-else-if="sopRuntime.active"><div class="step-current">等待算法处理</div><p>请启动算法，工作台将显示检测框对应的实时判定结果。</p></template>
        <template v-else><div class="step-current">未发布 SOP</div><p>请先在 SOP 配置页面保存并发布判断条件。</p></template>
        <div v-if="liveSteps.length" class="runtime-step-list"><span v-for="step in liveSteps" :key="step.id" :class="`is-${step.state}`">{{ step.index + 1 }}. {{ step.name }}<b>{{ stateLabel(step.state) }}</b></span></div>
        <button v-if="sopRuntime.active" class="ghost runtime-reset" :disabled="status.algorithm !== 'running'" @click="resetSopRuntime">重新开始当前 SOP</button>
        <div class="landmark">手部叠加总开关 <label class="switch"><input type="checkbox" :checked="handOverlaysEnabled" @change="toggleLandmarks($event.target.checked)"><i></i></label></div>
        <p class="muted">仅作为手部功能的快捷全开/全关；下方项目可单独控制</p>
        <div class="visualization-controls"><strong>画面叠加显示</strong><label v-for="item in visualizationItems" :key="item.key"><span>{{ item.label }}</span><input type="checkbox" :checked="visualization[item.key]" @change="setVisualization(item.key, $event.target.checked)"><i></i></label></div>
      </aside>
    </div>
    <div v-if="message" class="toast">{{ message }}</div>
    <div v-if="history" class="modal"><div class="modal-card"><div class="page-title"><h2>录像列表</h2><button class="ghost" @click="history = false">关闭</button></div><p v-if="!videos.length" class="muted">暂无已保存录像</p><div v-for="item in videos" :key="item.filename" class="video-item"><span>{{ item.filename }}<small>{{ item.size_mb }} MB · {{ new Date(item.modified_at * 1000).toLocaleString() }}</small></span><button @click="download(item.filename)">下载</button><button class="danger" @click="remove(item.filename)">删除</button></div></div></div>
  </section>
</template>

<script setup>
import { computed, nextTick, onMounted, onUnmounted, ref } from "vue";
import { api, post, request } from "../api";
import Metric from "../components/Metric.vue";

const video = ref(null); const status = ref({}); const sopRuntime = ref({ active: false }); const runtimeState = ref({});
const visualization = ref({ hand_landmarks: true, object_boxes: true, hand_box: false, skeleton: true, keypoints: true, debug_panel: true });
const landmarks = ref(true); const recordingOn = ref(false); const history = ref(false); const videos = ref([]);
const message = ref(""); const streamMessage = ref(""); const busy = ref(false);
const visualizationItems = [{ key: "object_boxes", label: "目标检测框" }, { key: "hand_box", label: "手部检测框" }, { key: "skeleton", label: "手部骨骼约束" }, { key: "keypoints", label: "21 个关键点" }, { key: "debug_panel", label: "算法调试面板" }];
const liveSteps = computed(() => runtimeState.value.steps || []);
const currentLiveStep = computed(() => liveSteps.value.find((step) => step.index === runtimeState.value.currentStepIndex) || liveSteps.value.find((step) => !step.completed));
const progress = computed(() => { if (!liveSteps.value.length) return 0; const completed = liveSteps.value.filter((step) => step.completed).length; return Math.round((completed / liveSteps.value.length) * 100); });
let timer; let peer = null; let connecting = false; let connectGeneration = 0;

const notify = (text) => { message.value = text; window.setTimeout(() => { message.value = ""; }, 2500); };

async function sync() {
  try {
    const [nextStatus, nextRuntime, nextState] = await Promise.all([request("/api/algorithm/status"), request("/api/sop/runtime"), request("/api/sop/runtime/state")]);
    status.value = nextStatus; sopRuntime.value = nextRuntime; runtimeState.value = nextState;
    if (status.value.visualization) visualization.value = { ...visualization.value, ...status.value.visualization };
    landmarks.value = visualization.value.hand_box && visualization.value.skeleton && visualization.value.keypoints;
    if (status.value.camera !== "running") closeStream();
    else if ((status.value.algorithm === "running" ? status.value.stream_ready : status.value.raw_stream_ready) && !peer && !connecting) { await nextTick(); await connectStream(status.value); }
  } catch (error) { streamMessage.value = error?.message || "无法读取运行状态"; }
}

async function camera(action) {
  busy.value = true; if (action === "stop") closeStream();
  try { status.value = await post(`/api/camera/${action}`, { resolution: "640x480" }); notify(action === "start" ? "摄像头已启动" : "摄像头已关闭"); await sync(); }
  catch (error) { notify(error.message); } finally { busy.value = false; }
}

async function algorithm(action) {
  busy.value = true;
  try { status.value = await post(`/api/algorithm/${action}`, { resolution: "640x480" }); notify(action === "start" ? "算法已启动" : "算法已关闭"); closeStream(); await sync(); }
  catch (error) { notify(error.message); } finally { busy.value = false; }
}

async function connectStream(current) {
  if (connecting || !video.value || current.camera !== "running") return;
  const path = current.algorithm === "running" ? "sop" : "raw"; const generation = ++connectGeneration; connecting = true; streamMessage.value = "正在连接实时视频流…";
  const connection = new RTCPeerConnection(); peer = connection;
  connection.ontrack = (event) => { if (generation !== connectGeneration || !video.value || !event.streams[0]) return; video.value.srcObject = event.streams[0]; void video.value.play().catch(() => undefined); streamMessage.value = ""; };
  connection.onconnectionstatechange = () => { if (["failed", "closed", "disconnected"].includes(connection.connectionState) && generation === connectGeneration) streamMessage.value = "实时视频流连接失败，请重试"; };
  connection.addTransceiver("video", { direction: "recvonly" });
  try {
    await connection.setLocalDescription(await connection.createOffer()); await waitForIce(connection);
    const response = await fetch(`${location.protocol}//${location.hostname}:8889/${path}/whep`, { method: "POST", headers: { "Content-Type": "application/sdp" }, body: connection.localDescription?.sdp || "" });
    if (!response.ok) throw new Error(`WebRTC 请求失败（HTTP ${response.status}）`);
    await connection.setRemoteDescription({ type: "answer", sdp: await response.text() });
  } catch (error) { if (generation === connectGeneration) streamMessage.value = error.message || "实时视频流连接失败"; connection.close(); if (peer === connection) peer = null; }
  finally { if (generation === connectGeneration) connecting = false; }
}

function closeStream() { connectGeneration += 1; peer?.close(); peer = null; connecting = false; if (video.value) { video.value.pause(); video.value.srcObject = null; } streamMessage.value = ""; }
const handOverlaysEnabled = computed(() => visualization.value.hand_box && visualization.value.skeleton && visualization.value.keypoints);
async function toggleLandmarks(enabled) { try { await request("/api/visualization/settings", { method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ hand_landmarks: enabled }) }); visualization.value = { ...visualization.value, hand_landmarks: enabled, hand_box: enabled, skeleton: enabled, keypoints: enabled }; landmarks.value = enabled; } catch (error) { notify(error.message); } }
async function setVisualization(key, enabled) { try { visualization.value = await request("/api/visualization/settings", { method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ [key]: enabled }) }); } catch (error) { notify(error.message); } }
async function resetSopRuntime() { try { await post("/api/sop/runtime/reset"); runtimeState.value = {}; notify("SOP 已请求重新开始"); } catch (error) { notify(error.message); } }
function stateLabel(state) { return ({ waiting: "等待条件", confirming: "确认中", completed: "已完成", timeout: "已超时", stale: "数据过期", finished: "全部完成" })[state] || state || "等待中"; }
async function recording() { busy.value = true; try { status.value = await post(`/api/recording/${recordingOn.value ? "stop" : "start"}`, { mode: "processed" }); recordingOn.value = !recordingOn.value; notify(recordingOn.value ? "已开始录制" : "录像已保存"); } catch (error) { notify(error.message); } finally { busy.value = false; } }
async function loadVideos() { try { videos.value = (await request("/api/recordings")).items || []; } catch (error) { notify(error.message); } }
function download(filename) { const anchor = document.createElement("a"); anchor.href = `${api}/api/recordings/${encodeURIComponent(filename)}/download`; anchor.download = filename; anchor.click(); }
async function remove(filename) { if (!confirm(`确认删除 ${filename}？`)) return; await fetch(`${api}/api/recordings/${encodeURIComponent(filename)}`, { method: "DELETE" }); await loadVideos(); }
onMounted(async () => { void sync(); void loadVideos(); try { visualization.value = await request("/api/visualization/settings"); landmarks.value = handOverlaysEnabled.value; } catch (_) {} timer = window.setInterval(() => void sync(), 500); });
onUnmounted(() => { if (timer) window.clearInterval(timer); closeStream(); });
function waitForIce(connection) { if (connection.iceGatheringState === "complete") return Promise.resolve(); return new Promise((resolve) => { const finish = () => { window.clearTimeout(timeout); connection.removeEventListener("icegatheringstatechange", changed); resolve(); }; const changed = () => { if (connection.iceGatheringState === "complete") finish(); }; const timeout = window.setTimeout(finish, 3000); connection.addEventListener("icegatheringstatechange", changed); }); }
</script>
