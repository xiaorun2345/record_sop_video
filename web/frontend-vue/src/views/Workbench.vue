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
            <label class="source-picker">输入源
              <select v-model="inputSource" :disabled="busy || status.camera === 'running'" @change="handleInputSourceChange">
                <option value="camera">主相机</option><option value="video">服务器录像</option>
              </select>
            </label>
            <label v-if="inputSource === 'camera'" class="source-picker">分辨率
              <select v-model="selectedResolution" :disabled="busy || status.camera === 'running'" @change="changeResolution"><option value="1920x1080">1920 × 1080</option><option value="640x480">640 × 480</option></select>
            </label>
            <label v-else class="source-picker recording-picker">服务器录像
              <select v-model="inputUri" :disabled="busy || status.camera === 'running'" @change="chooseRecordedVideo">
                <option value="">选择已录制视频</option>
                <option v-for="item in videos" :key="item.filename" :value="recordingUri(item.filename)">{{ item.filename }}</option>
              </select>
            </label>
            <button @click="camera('start')" :disabled="busy || status.camera === 'running'">{{ inputSource === 'video' ? '播放服务器录像' : '启动摄像头' }}</button>
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
            <div v-for="item in currentLiveStep.objects" :key="item.id || item.label" class="runtime-check" :class="{ ok: Number(item.bestCount || 0) >= Number(item.requiredCount || 0) }">
              <strong>{{ item.label }}</strong><em>{{ item.bestCount || item.currentCount || 0 }} / {{ item.requiredCount }}</em>
            </div>
            <div v-if="currentLiveStep.handRoiConfigured" class="runtime-check" :class="{ ok: currentLiveStep.handRoiSatisfied }"><strong>手部作业区域</strong><em>{{ currentLiveStep.handRoiSatisfied ? '已满足' : '待满足' }}</em></div>
            <div v-if="!currentLiveStep.spatialSatisfied" class="runtime-check"><strong>手物空间距离</strong><em>待满足</em></div>
            <div v-if="!currentLiveStep.objects?.length" class="runtime-empty">当前步骤没有可判断的必检对象</div>
          </div>
          <div v-if="runtimeState.alerts?.length" class="runtime-alert"><strong>{{ runtimeState.alerts[0].message }}</strong><small>{{ alarmLight.connected ? "已触发串口报警灯" : "报警灯未连接，已在页面预警" }}</small></div>
          <div v-if="videoAnalysisEnded" class="shipment-result" :class="{ ok: !missingShipmentItems.length }">
            <strong>{{ missingShipmentItems.length ? "视频分析结束，发现漏发货" : "视频分析结束，未发现漏发货" }}</strong>
            <p v-if="missingShipmentItems.length">以下物品未达到本次发货要求数量：</p>
            <div v-if="missingShipmentItems.length" class="missing-list">
              <span v-for="item in missingShipmentItems" :key="`${item.stepId}-${item.label}`">
                {{ item.name }} <b>{{ item.bestCount }} / {{ item.requiredCount }}</b>
              </span>
            </div>
            <small v-else>所有必检物品均已在视频中识别到要求数量。</small>
          </div>
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
import { selectVideoSource, setCameraResolution } from "../api/camera";

const video = ref(null); const status = ref({}); const sopRuntime = ref({ active: false }); const runtimeState = ref({}); const alarmLight = ref({ connected: false });
const visualization = ref({ hand_landmarks: true, object_boxes: true, hand_box: false, skeleton: true, keypoints: true, debug_panel: true });
const landmarks = ref(true); const recordingOn = ref(false); const history = ref(false); const videos = ref([]);
const message = ref(""); const streamMessage = ref(""); const busy = ref(false);
const selectedResolution = ref("640x480"); const inputSource = ref("camera"); const inputUri = ref(""); const inputName = ref("");
const sourceSelectionPending = ref(false);
const visualizationItems = [{ key: "object_boxes", label: "目标检测框" }, { key: "hand_box", label: "手部检测框" }, { key: "skeleton", label: "手部骨骼约束" }, { key: "keypoints", label: "21 个关键点" }, { key: "debug_panel", label: "算法调试面板" }];
const liveSteps = computed(() => runtimeState.value.steps || []);
const currentLiveStep = computed(() => liveSteps.value.find((step) => step.index === runtimeState.value.currentStepIndex) || liveSteps.value.find((step) => !step.completed));
const progress = computed(() => { if (!liveSteps.value.length) return 0; const completed = liveSteps.value.filter((step) => step.completed).length; return Math.round((completed / liveSteps.value.length) * 100); });
const videoAnalysisEnded = computed(() => inputSource.value === "video" && status.value.video_finished && liveSteps.value.length > 0 && runtimeState.value.frameId > 0);
const missingShipmentItems = computed(() => liveSteps.value.flatMap((step) => (step.objects || []).map((item) => {
  const bestCount = Math.max(Number(item.bestCount || 0), Number(item.currentCount || 0));
  return { stepId: step.id, label: item.label, name: shipmentLabel(item.label), bestCount, requiredCount: Number(item.requiredCount || 0) };
})).filter((item) => item.bestCount < item.requiredCount));
let timer; let peer = null; let connecting = false; let connectGeneration = 0;

const shipmentLabels = { cover_cloth: "盖布", long_handle: "长柄", manual: "说明书", padding_board: "垫板", small_red_lever: "小红杆", top_pad: "上垫", vertical_support_bracket: "竖向支撑架" };
function shipmentLabel(label) { return shipmentLabels[label] || label; }
const notify = (text) => { message.value = text; window.setTimeout(() => { message.value = ""; }, 2500); };

async function sync() {
  try {
    const [nextStatus, nextRuntime, nextState, nextAlarmLight] = await Promise.all([request("/api/algorithm/status"), request("/api/sop/runtime"), request("/api/sop/runtime/state"), request("/api/peripherals/alarm-light")]);
    status.value = nextStatus; sopRuntime.value = nextRuntime; runtimeState.value = nextState; alarmLight.value = nextAlarmLight;
    selectedResolution.value = status.value.resolution || selectedResolution.value;
    if (!sourceSelectionPending.value) {
      inputSource.value = status.value.input_type === "video" ? "video" : "camera";
      inputUri.value = status.value.input_uri || ""; inputName.value = inputUri.value.split("/").pop() || "";
    }
    if (status.value.visualization) visualization.value = { ...visualization.value, ...status.value.visualization };
    landmarks.value = visualization.value.hand_box && visualization.value.skeleton && visualization.value.keypoints;
    if (status.value.camera !== "running") closeStream();
    else if ((status.value.algorithm === "running" ? status.value.stream_ready : status.value.raw_stream_ready) && !peer && !connecting) { await nextTick(); await connectStream(status.value); }
  } catch (error) { streamMessage.value = error?.message || "无法读取运行状态"; }
}

async function camera(action) {
  busy.value = true; if (action === "stop") closeStream();
  const options = inputSource.value === "video" ? {} : { resolution: selectedResolution.value };
  try { status.value = await post(`/api/camera/${action}`, options); notify(action === "start" ? (inputSource.value === "video" ? "服务器录像已打开" : "摄像头已启动") : "输入源已关闭"); await sync(); }
  catch (error) { notify(error.message); } finally { busy.value = false; }
}

async function changeResolution() {
  if (status.value.camera === "running") return;
  try {
    status.value = await setCameraResolution(selectedResolution.value);
    selectedResolution.value = status.value.resolution || selectedResolution.value;
    notify(`分辨率已设置为 ${selectedResolution.value}`);
  } catch (error) {
    notify(error?.message || "分辨率设置失败");
    await sync();
  }
}

async function algorithm(action) {
  busy.value = true;
  const options = inputSource.value === "video" ? {} : { resolution: selectedResolution.value };
  try { status.value = await post(`/api/algorithm/${action}`, options); notify(action === "start" ? "算法已启动" : "算法已关闭"); closeStream(); await sync(); }
  catch (error) { notify(error.message); } finally { busy.value = false; }
}

async function changeInputSource() {
  if (status.value.camera === "running") { notify("请先关闭摄像头"); return; }
  if (inputSource.value === "video" && !inputUri.value) { notify("请先选择服务器录像"); return; }
  try {
    status.value = await selectVideoSource(inputSource.value === "video" ? "video" : "camera", inputSource.value === "video" ? inputUri.value : "");
    sourceSelectionPending.value = false;
    notify(inputSource.value === "video" ? "已选择服务器录像" : "已切换到主相机");
  } catch (error) { sourceSelectionPending.value = false; notify(error?.message || "输入源切换失败"); await sync(); }
}

async function handleInputSourceChange() {
  sourceSelectionPending.value = true;
  if (inputSource.value === "video" && !inputUri.value) {
    inputName.value = "";
    notify("请选择要分析的服务器录像");
    return;
  }
  if (inputSource.value === "camera") { inputUri.value = ""; inputName.value = ""; }
  await changeInputSource();
}

function recordingUri(filename) { return `/home/armsom/record_sop_video/output/recordings/${filename}`; }
async function chooseRecordedVideo() { inputName.value = inputUri.value.split("/").pop() || ""; if (inputUri.value) await changeInputSource(); }

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
async function loadVideos() { try { videos.value = (await request("/api/recordings")).items || []; if (inputSource.value === "video" && !videos.value.some((item) => recordingUri(item.filename) === inputUri.value)) { inputUri.value = ""; inputName.value = ""; } } catch (error) { notify(error.message); } }
function download(filename) { const anchor = document.createElement("a"); anchor.href = `${api}/api/recordings/${encodeURIComponent(filename)}/download`; anchor.download = filename; anchor.click(); }
async function remove(filename) { if (!confirm(`确认删除 ${filename}？`)) return; await fetch(`${api}/api/recordings/${encodeURIComponent(filename)}`, { method: "DELETE" }); await loadVideos(); }
onMounted(async () => { void sync(); void loadVideos(); try { visualization.value = await request("/api/visualization/settings"); landmarks.value = handOverlaysEnabled.value; } catch (_) {} timer = window.setInterval(() => void sync(), 500); });
onUnmounted(() => { if (timer) window.clearInterval(timer); closeStream(); });
function waitForIce(connection) { if (connection.iceGatheringState === "complete") return Promise.resolve(); return new Promise((resolve) => { const finish = () => { window.clearTimeout(timeout); connection.removeEventListener("icegatheringstatechange", changed); resolve(); }; const changed = () => { if (connection.iceGatheringState === "complete") finish(); }; const timeout = window.setTimeout(finish, 3000); connection.addEventListener("icegatheringstatechange", changed); }); }
</script>
