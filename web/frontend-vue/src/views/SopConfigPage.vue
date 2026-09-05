<script setup lang="ts">
import { computed, onMounted, reactive, ref, watch } from "vue";
import { storeToRefs } from "pinia";
import {
  ArrowRight,
  Camera,
  Check,
  Connection,
  Cpu,
  Delete,
  EditPen,
  FullScreen,
  Menu,
  Operation,
  Plus,
  QuestionFilled,
  Search,
  SwitchButton,
  Upload,
  VideoCamera,
  ZoomIn,
} from "@element-plus/icons-vue";
import { ElMessage } from "element-plus/es/components/message/index";
import { ElMessageBox } from "element-plus/es/components/message-box/index";
import { useSopStore } from "@/stores/sop";
import { createId, createSopJudgementConfig } from "@/domain/sop";
import { useCameraStream } from "@/composables/useCameraStream";
import { polygonPoints, useRoiEditor } from "@/composables/useRoiEditor";
import type { RequiredObject, RequiredObjectRelation, SopDefinition, SopExecutionMode } from "@/types/sop";
import SopSummaryCard from "@/components/sop/SopSummaryCard.vue";
import { getRecordings, selectVideoSource, setCameraResolution, type RecordingItem } from "@/api/camera";

const sopStore = useSopStore();
const {
  sops,
  activeSop,
  activeSopId,
  steps,
  activeStep,
  activeStepId,
  draftChanged,
  errorMessage,
} = storeToRefs(sopStore);

const keyword = ref("");
const publishing = ref(false);
const sopDialogVisible = ref(false);
const sopDialogMode = ref<"create" | "edit">("create");
const sopFormName = ref("");
const sopFormDescription = ref("");
const sopFormExecutionMode = ref<SopExecutionMode | "">("");
const stepDialogVisible = ref(false);
const stepFormName = ref("");
const stepFormDescription = ref("");
const dragFrom = ref<number | null>(null);
const fullscreen = ref(false);
const zoomed = ref(false);
const {
  videoElement,
  cameraStatus,
  cameraBusy,
  cameraRunning,
  cameraStateLabel,
  streamConnecting,
  streamReady,
  streamError,
  videoReady,
  videoWidth,
  videoHeight,
  updateVideoMetadata,
  startCamera,
  stopCamera,
  selectedResolution,
} = useCameraStream();
const inputSource = ref<"camera" | "video">("camera");
const localVideoUri = ref("");
const localVideoName = ref("");
const recordedVideos = ref<RecordingItem[]>([]);
const sourceBusy = ref(false);
const sourceSelectionPending = ref(false);
watch(cameraStatus, (status) => {
  if (!status || sourceSelectionPending.value) return;
  inputSource.value = status.input_type === "video" ? "video" : "camera";
  localVideoUri.value = status.input_uri || "";
  localVideoName.value = localVideoUri.value.split("/").pop() || "";
}, { immediate: true });
const roiViewBox = computed(() => `0 0 ${videoWidth.value || 1} ${videoHeight.value || 1}`);
const roiPointRadius = computed(() => Math.max(1, Math.min(videoWidth.value || 100, videoHeight.value || 100) * 0.011));
const requiredObjectDialogVisible = ref(false);
const requiredObjectDialogMode = ref<"create" | "edit">("create");
const editingRequiredObjectIndex = ref(-1);
const requiredObjectForm = reactive<{
  name: string;
  field: string;
  quantity: number;
  roiIds: string[];
  relationTargetId: string;
  relationType: RequiredObjectRelation["type"] | "";
}>({
  name: "",
  field: "",
  quantity: 1,
  roiIds: [],
  relationTargetId: "",
  relationType: "",
});

const {
  roiAreas,
  activeRoi,
  totalRoiPointCount,
  roiOptions,
  pendingRoiPointCount,
  roiSelectionActive,
  pendingRoiId,
  beginSelection,
  cancelSelection,
  handleFrameContextMenu,
  startPointDrag,
  addRoiPoint,
  removeRoiPoint,
  movePoint,
  stopPointDrag,
  clearRoi: clearAllRoi,
} = useRoiEditor({
  activeStep,
  videoReady,
  videoElement,
  onChanged: () => sopStore.markChanged(),
  onWarning: (message) => ElMessage.warning(message),
  onSelectionSaved: (roiId) => {
    if (!requiredObjectForm.roiIds.includes(roiId)) requiredObjectForm.roiIds.push(roiId);
    requiredObjectDialogVisible.value = true;
    ElMessage.success("ROI 位置已添加");
  },
});

onMounted(() => {
  void sopStore.initialize().catch((error) => ElMessage.error(getErrorMessage(error)));
  void loadRecordedVideos();
});

const filteredSteps = computed(() => {
  const normalized = keyword.value.trim().toLowerCase();
  if (!normalized) return steps.value;
  return steps.value.filter(
    (step) => step.name.toLowerCase().includes(normalized) || step.id.toLowerCase().includes(normalized),
  );
});

const availableRelationObjects = computed(() => {
  const editingId = editingRequiredObjectIndex.value >= 0
    ? activeStep.value.requiredObjects[editingRequiredObjectIndex.value]?.id
    : "";
  return activeSop.value.steps.flatMap((step) => step.requiredObjects
    .filter((object) => object.id !== editingId)
    .map((object) => ({
      id: object.id,
      label: `${step.name} / ${object.name}`,
    })));
});

const flowIcons = [SwitchButton, Camera, Cpu, Check, Upload];

function beginReorder(stepId: string, event: DragEvent) {
  dragFrom.value = steps.value.findIndex((step) => step.id === stepId);
  event.dataTransfer?.setData("text/plain", stepId);
  if (event.dataTransfer) event.dataTransfer.effectAllowed = "move";
}

function finishReorder(stepId: string) {
  const targetIndex = steps.value.findIndex((step) => step.id === stepId);
  if (dragFrom.value !== null) sopStore.reorder(dragFrom.value, targetIndex);
  dragFrom.value = null;
}

function openCreateStep() {
  stepFormName.value = "";
  stepFormDescription.value = "";
  stepDialogVisible.value = true;
}

function submitStep() {
  const name = stepFormName.value.trim();
  const description = stepFormDescription.value.trim();
  if (!name) {
    ElMessage.warning("请输入步骤名称");
    return;
  }
  if (!description) {
    ElMessage.warning("请输入步骤描述");
    return;
  }
  if (steps.value.some((step) => step.name.toLowerCase() === name.toLowerCase())) {
    ElMessage.warning("步骤名称已存在，请使用其他名称");
    return;
  }
  sopStore.createStep(name, description);
  keyword.value = "";
  stepDialogVisible.value = false;
  ElMessage.success("步骤已添加，请在右侧继续配置参数");
}

async function deleteStep() {
  const confirmed = await ElMessageBox.confirm(
    `确认删除“${activeStep.value.name}”吗？该步骤的 ROI 和必检对象配置也会一并删除。`,
    "删除步骤",
    { confirmButtonText: "确认删除", cancelButtonText: "取消", type: "warning" },
  ).then(() => true).catch(() => false);
  if (!confirmed) return;
  sopStore.deleteActiveStep();
  ElMessage.success("步骤已删除");
}

async function publishSop() {
  if (publishing.value) return;
  const confirmed = await ElMessageBox.confirm(
    "将校验并发布当前流程，发布成功后自动下载用于目标检测结果匹配的判断条件 JSON。确认继续吗？",
    "保存并发布",
    { confirmButtonText: "发布并下载", cancelButtonText: "取消", type: "warning" },
  ).then(() => true).catch(() => false);
  if (!confirmed) return;
  publishing.value = true;
  try {
    const issues = await sopStore.validateActive();
    if (issues.length) {
      ElMessage.error(issues[0].message);
      return;
    }
    const { published, savedLocally } = await sopStore.publishActive();
    downloadJudgementConfig(published);
    if (savedLocally) {
      ElMessage.success(`版本 ${published.version} 已发布并在前端保存，判断条件 JSON 已下载`);
    } else {
      ElMessage.warning(`版本 ${published.version} 已发布且 JSON 已下载，但浏览器未允许本地保存`);
    }
  } catch (error) {
    ElMessage.error(getErrorMessage(error));
  } finally {
    publishing.value = false;
  }
}

function downloadJudgementConfig(sop: SopDefinition) {
  const judgementConfig = createSopJudgementConfig(sop);
  const payload = JSON.stringify(judgementConfig, null, 2);
  const blob = new Blob([payload], { type: "application/json;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = `${sop.id.toLowerCase()}-judgement.json`;
  document.body.append(anchor);
  anchor.click();
  anchor.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 0);
}

function openCreateSop() {
  sopDialogMode.value = "create";
  sopFormName.value = "";
  sopFormDescription.value = "";
  sopFormExecutionMode.value = "";
  sopDialogVisible.value = true;
}

function openEditSop() {
  sopDialogMode.value = "edit";
  sopFormName.value = activeSop.value.name;
  sopFormDescription.value = activeSop.value.description;
  sopFormExecutionMode.value = activeSop.value.executionMode;
  sopDialogVisible.value = true;
}

async function submitSop() {
  const name = sopFormName.value.trim();
  if (!name) {
    ElMessage.warning("请输入 SOP 名称");
    return;
  }
  if (!sopFormExecutionMode.value) {
    ElMessage.warning("请选择步骤执行方式");
    return;
  }
  const duplicate = sops.value.some(
    (sop) =>
      sop.name.toLowerCase() === name.toLowerCase()
      && (sopDialogMode.value === "create" || sop.id !== activeSopId.value),
  );
  if (duplicate) {
    ElMessage.warning("SOP 名称已存在，请使用其他名称");
    return;
  }
  try {
    if (sopDialogMode.value === "create") {
      await sopStore.createSop(name, sopFormDescription.value.trim(), sopFormExecutionMode.value);
      ElMessage.success("SOP 流程已创建");
    } else {
      await sopStore.updateSop(
        activeSopId.value,
        name,
        sopFormDescription.value.trim(),
        sopFormExecutionMode.value,
      );
      ElMessage.success("SOP 流程已更新");
    }
  } catch (error) {
    ElMessage.error(getErrorMessage(error));
    return;
  }
  keyword.value = "";
  sopDialogVisible.value = false;
}

async function deleteSop() {
  if (sops.value.length <= 1) {
    ElMessage.warning("至少需要保留一个 SOP 流程");
    return;
  }
  const confirmed = await ElMessageBox.confirm(
    `确认删除“${activeSop.value.name}”吗？该流程下的步骤草稿也会一并移除。`,
    "删除 SOP 流程",
    { confirmButtonText: "确认删除", cancelButtonText: "取消", type: "warning" },
  ).then(() => true).catch(() => false);
  if (!confirmed) return;
  try {
    await sopStore.deleteSop(activeSopId.value);
  } catch (error) {
    ElMessage.error(getErrorMessage(error));
    return;
  }
  keyword.value = "";
  ElMessage.success("SOP 流程已删除");
}

function beginRequiredObjectRoiSelection() {
  if (!videoReady.value) {
    ElMessage.warning("请先打开摄像头并等待实时画面显示");
    return;
  }
  requiredObjectDialogVisible.value = false;
  beginSelection();
  ElMessage.info("请双击画面添加顶点，并在多边形内部右键保存");
}

function cancelRequiredObjectRoiSelection() {
  cancelSelection();
  requiredObjectDialogVisible.value = true;
}

function clearRoi() {
  const wasSelecting = roiSelectionActive.value;
  clearAllRoi();
  if (wasSelecting) requiredObjectDialogVisible.value = true;
}

function resetRequiredObjectForm() {
  requiredObjectForm.name = "";
  requiredObjectForm.field = "";
  requiredObjectForm.quantity = 1;
  requiredObjectForm.roiIds = [];
  requiredObjectForm.relationTargetId = "";
  requiredObjectForm.relationType = "";
}

function openCreateRequiredObject() {
  requiredObjectDialogMode.value = "create";
  editingRequiredObjectIndex.value = -1;
  resetRequiredObjectForm();
  requiredObjectDialogVisible.value = true;
}

function openEditRequiredObject(index: number) {
  const object = activeStep.value.requiredObjects[index];
  requiredObjectDialogMode.value = "edit";
  editingRequiredObjectIndex.value = index;
  requiredObjectForm.name = object.name;
  requiredObjectForm.field = object.field;
  requiredObjectForm.quantity = object.quantity;
  requiredObjectForm.roiIds = [...object.roiIds];
  requiredObjectForm.relationTargetId = object.relation?.targetObjectId ?? "";
  requiredObjectForm.relationType = object.relation?.type ?? "";
  requiredObjectDialogVisible.value = true;
}

function submitRequiredObject() {
  const name = requiredObjectForm.name.trim();
  const field = requiredObjectForm.field.trim();
  if (!name || !field) {
    ElMessage.warning("请填写必检对象名称和字段");
    return;
  }
  const duplicate = activeStep.value.requiredObjects.some(
    (object, index) => object.field.toLowerCase() === field.toLowerCase() && index !== editingRequiredObjectIndex.value,
  );
  if (duplicate) {
    ElMessage.warning("必检对象字段不能重复");
    return;
  }
  if (requiredObjectForm.relationTargetId && !requiredObjectForm.relationType) {
    ElMessage.warning("请选择与关联对象的关系类型");
    return;
  }
  const value: RequiredObject = {
    id: requiredObjectDialogMode.value === "edit"
      ? activeStep.value.requiredObjects[editingRequiredObjectIndex.value].id
      : createId("OBJ"),
    name,
    field,
    quantity: requiredObjectForm.quantity,
    roiIds: [...requiredObjectForm.roiIds],
    relation: requiredObjectForm.relationTargetId && requiredObjectForm.relationType
      ? {
          targetObjectId: requiredObjectForm.relationTargetId,
          type: requiredObjectForm.relationType,
        }
      : null,
  };
  if (requiredObjectDialogMode.value === "edit") {
    activeStep.value.requiredObjects.splice(editingRequiredObjectIndex.value, 1, value);
  } else {
    activeStep.value.requiredObjects.push(value);
  }
  requiredObjectDialogVisible.value = false;
  sopStore.markChanged();
  ElMessage.success(requiredObjectDialogMode.value === "edit" ? "必检对象已更新" : "必检对象已添加");
}

async function deleteRequiredObject(index: number) {
  const object = activeStep.value.requiredObjects[index];
  const confirmed = await ElMessageBox.confirm(
    `确认删除必检对象“${object.name}”吗？`,
    "删除必检对象",
    { confirmButtonText: "确认删除", cancelButtonText: "取消", type: "warning" },
  ).then(() => true).catch(() => false);
  if (!confirmed) return;
  activeStep.value.requiredObjects.splice(index, 1);
  activeSop.value.steps.forEach((step) => {
    step.requiredObjects.forEach((item) => {
      if (item.relation?.targetObjectId === object.id) item.relation = null;
    });
  });
  sopStore.markChanged();
  ElMessage.success("必检对象已删除");
}

function objectPositionLabel(object: RequiredObject) {
  if (!object.roiIds.length) return "不限位置";
  return object.roiIds
    .map((id) => roiAreas.value.find((area) => area.id === id)?.name)
    .filter(Boolean)
    .join("、") || "不限位置";
}

function objectRelationLabel(object: RequiredObject) {
  if (!object.relation) return "";
  for (const step of activeSop.value.steps) {
    const target = step.requiredObjects.find((item) => item.id === object.relation?.targetObjectId);
    if (target) return `重叠 ${step.name} / ${target.name}`;
  }
  return "";
}

function handleRelationTargetChange(value: string) {
  requiredObjectForm.relationType = value ? "overlaps" : "";
}

async function handleCameraStart() {
  const error = await startCamera(selectedResolution.value);
  if (error) ElMessage.error(error);
  else ElMessage.success(inputSource.value === "video" ? "服务器录像已打开" : "摄像头已打开");
}

async function handleResolutionChange() {
  if (cameraRunning.value) return;
  try {
    const status = await setCameraResolution(selectedResolution.value);
    selectedResolution.value = status.resolution || selectedResolution.value;
    ElMessage.success(`分辨率已设置为 ${selectedResolution.value}`);
  } catch (error) {
    ElMessage.error(getErrorMessage(error));
  }
}

function recordingUri(filename: string) {
  return `/home/armsom/record_sop_video/output/recordings/${filename}`;
}

async function loadRecordedVideos() {
  try {
    recordedVideos.value = (await getRecordings()).items || [];
    if (inputSource.value === "video" && !recordedVideos.value.some((item) => recordingUri(item.filename) === localVideoUri.value)) {
      localVideoUri.value = "";
      localVideoName.value = "";
    }
  } catch (error) {
    ElMessage.error(getErrorMessage(error));
  }
}

async function selectInputSource(type: "camera" | "video", uri = "") {
  if (type === "video" && !uri) {
    ElMessage.warning("请先选择服务器录像");
    return;
  }
  sourceBusy.value = true;
  try {
    if (cameraRunning.value) await stopCamera();
    await selectVideoSource(type, uri);
    sourceSelectionPending.value = false;
    inputSource.value = type;
    localVideoUri.value = uri;
    localVideoName.value = uri.split("/").pop() || "";
    ElMessage.success(type === "video" ? "已选择服务器录像" : "已切换到主相机");
  } catch (error) {
    sourceSelectionPending.value = false;
    ElMessage.error(getErrorMessage(error));
  } finally { sourceBusy.value = false; }
}

async function handleInputSourceChange(type: "camera" | "video") {
  sourceSelectionPending.value = true;
  if (type === "video") {
    await loadRecordedVideos();
    if (!localVideoUri.value) {
      ElMessage.info("请先选择服务器录像");
      return;
    }
  }
  await selectInputSource(type, type === "video" ? localVideoUri.value : "");
}

async function handleRecordedVideoChange(uri: string) {
  if (!uri) return;
  sourceSelectionPending.value = true;
  await selectInputSource("video", uri);
}

function handleRecordingDropdownVisible(visible: boolean) {
  if (visible) void loadRecordedVideos();
}


async function handleCameraStop() {
  const error = await stopCamera();
  if (error) ElMessage.error(error);
  else ElMessage.success("摄像头已关闭");
}

function markChanged() {
  sopStore.markChanged();
}

function getErrorMessage(error: unknown) {
  return error instanceof Error ? error.message : "操作失败，请稍后重试";
}

</script>

<template>
  <section class="page-content" v-loading="sopStore.loading" element-loading-text="正在读取 SOP 配置…">
        <div v-if="errorMessage" class="service-error" role="alert">
          <strong>SOP 服务请求失败</strong>
          <span>{{ errorMessage }}</span><el-button @click="sopStore.initialize().catch(error => ElMessage.error(getErrorMessage(error)))">重试</el-button>
        </div>
        <section class="content-header">
          <div>
            <div class="title-line">
              <h2>SOP步骤与流程</h2>
              <span v-if="draftChanged" class="draft-badge">有未保存更改</span>
            </div>
            <el-select
              :model-value="activeSopId"
              class="sop-select"
              size="small"
              aria-label="选择 SOP"
              @change="sopStore.selectSop"
            >
              <el-option v-for="sop in sops" :key="sop.id" :label="sop.name" :value="sop.id" />
            </el-select>
            <div class="sop-crud-actions" aria-label="SOP 流程操作">
              <el-tooltip content="新建 SOP" placement="top"><el-button :icon="Plus" aria-label="新建 SOP" @click="openCreateSop" /></el-tooltip>
              <el-tooltip content="编辑当前 SOP" placement="top"><el-button :icon="EditPen" aria-label="编辑当前 SOP" @click="openEditSop" /></el-tooltip>
              <el-tooltip content="删除当前 SOP" placement="top"><el-button :icon="Delete" aria-label="删除当前 SOP" @click="deleteSop" /></el-tooltip>
            </div>
          </div>
          <div class="header-actions">
            <el-button type="primary" :icon="Upload" :loading="publishing" :disabled="!sops.length || sopStore.loading" @click="publishSop">保存并发布</el-button>
          </div>
        </section>

        <div class="main-grid">
          <div class="sop-workspace-grid">
          <aside class="step-panel panel">
            <div class="step-tools">
              <el-input v-model="keyword" :prefix-icon="Search" placeholder="搜索步骤" clearable />
              <el-button type="primary" plain :icon="Plus" aria-label="新增步骤" @click="openCreateStep" />
            </div>
            <div class="step-list">
              <button
                v-for="step in filteredSteps"
                :key="step.id"
                class="step-item"
                :class="{ active: activeStepId === step.id }"
                type="button"
                draggable="true"
                @click="sopStore.selectStep(step.id)"
                @dragstart="beginReorder(step.id, $event)"
                @dragover.prevent
                @drop="finishReorder(step.id)"
              >
                <el-icon class="drag-handle"><Menu /></el-icon>
                <span class="step-number">{{ steps.findIndex((item) => item.id === step.id) + 1 }}</span>
                <span class="step-copy"><strong>{{ step.name }}</strong><small>{{ step.id }}</small></span>
                <span class="enabled-state"><i></i>{{ step.enabled ? "已启用" : "已停用" }}</span>
              </button>
            </div>
            <button class="new-step-button" type="button" @click="openCreateStep"><el-icon><Plus /></el-icon>添加步骤</button>
            <div class="list-tip">拖拽步骤可调整执行顺序</div>
          </aside>

          <section class="center-column">
            <div class="flow-panel panel">
              <div v-if="!steps.length" class="flow-empty-state">
                <span><el-icon><Operation /></el-icon></span>
                <div>
                  <strong>当前流程还没有步骤</strong>
                </div>
                <el-button type="primary" plain :icon="Plus" @click="openCreateStep">添加步骤</el-button>
              </div>
              <div v-else class="flow-track">
                <template v-for="(step, index) in steps" :key="step.id">
                  <button
                    class="flow-step"
                    :class="{ active: activeStepId === step.id }"
                    type="button"
                    @click="sopStore.selectStep(step.id)"
                  >
                    <span class="flow-icon"><el-icon :size="28"><component :is="flowIcons[index % flowIcons.length]" /></el-icon></span>
                    <strong>{{ step.name }}</strong>
                    <small><i></i>{{ step.enabled ? "已启用" : "已停用" }}</small>
                    <em>超时: {{ step.timeout }}s</em>
                  </button>
                  <el-icon v-if="index < steps.length - 1" class="flow-arrow"><ArrowRight /></el-icon>
                </template>
              </div>
            </div>

            <div v-if="steps.length" class="editor-panel panel">
              <div class="panel-title-row">
                <h3>步骤配置 - {{ activeStep.name }}</h3>
                <el-button text :icon="Delete" @click="deleteStep">删除</el-button>
              </div>

              <div class="camera-section-heading">相机画面</div>

              <div class="camera-layout">
                <div>
                  <div
                    class="camera-frame"
                    :class="{ fullscreen, zoomed }"
                    @dblclick="addRoiPoint"
                    @contextmenu="handleFrameContextMenu"
                    @pointermove="movePoint"
                    @pointerup="stopPointDrag"
                    @pointercancel="stopPointDrag"
                  >
                    <video
                      v-if="cameraRunning"
                      ref="videoElement"
                      class="camera-video"
                      autoplay
                      muted
                      playsinline
                      @loadedmetadata="updateVideoMetadata"
                      @resize="updateVideoMetadata"
                    ></video>
                    <div v-else class="video-empty-state">
                      <span class="video-empty-icon"><el-icon><VideoCamera /></el-icon></span>
                      <strong>摄像头未打开</strong>
                      <p>{{ streamError || '打开 RK3588 摄像头后，将在这里显示实时视频流' }}</p>
                      <el-button type="primary" :loading="cameraBusy" @click="handleCameraStart">{{ inputSource === 'video' ? '播放录像' : '打开摄像头' }}</el-button>
                    </div>
                    <div v-if="cameraRunning" class="camera-status"><span></span>{{ streamReady ? '实时画面' : '连接中' }}</div>
                    <div v-if="roiSelectionActive" class="roi-drawing-banner">
                      <div>
                        <strong>正在添加必检对象位置</strong>
                        <span>当前已选 {{ pendingRoiPointCount }} 个点，双击添加，区域内右键保存</span>
                      </div>
                      <button type="button" @click="cancelRequiredObjectRoiSelection">取消标注</button>
                    </div>
                    <div class="camera-controls">
                      <button type="button" aria-label="放大" @click="zoomed = !zoomed"><el-icon><ZoomIn /></el-icon></button>
                      <button type="button" aria-label="全屏" @click="fullscreen = !fullscreen"><el-icon><FullScreen /></el-icon></button>
                    </div>
                    <svg
                      v-if="videoReady && totalRoiPointCount"
                      class="roi-layer"
                      :viewBox="roiViewBox"
                      preserveAspectRatio="xMidYMid meet"
                    >
                      <template v-for="area in roiAreas" :key="area.id">
                        <polygon
                          v-if="area.points.length >= 3"
                          :points="polygonPoints(area.points, videoWidth, videoHeight)"
                          :class="{ inactive: area.id !== activeRoi?.id }"
                        />
                        <polyline v-else :points="polygonPoints(area.points, videoWidth, videoHeight)" />
                      </template>
                      <template v-for="area in roiAreas" :key="`points-${area.id}`">
                        <circle
                        v-for="(point, pointIndex) in area.points"
                        :key="`${area.id}-${pointIndex}`"
                        class="roi-point"
                        :class="{
                          inactive: area.id !== activeRoi?.id,
                          locked: roiSelectionActive && area.id !== pendingRoiId,
                        }"
                        :cx="point.x * (videoWidth || 1)"
                        :cy="point.y * (videoHeight || 1)"
                        :r="roiPointRadius"
                        role="button"
                        tabindex="0"
                        :aria-label="`${area.name} 顶点 ${pointIndex + 1}`"
                        @pointerdown.stop="startPointDrag(area.id, pointIndex, $event)"
                        @dblclick.stop
                        @contextmenu.prevent.stop="removeRoiPoint(area.id, pointIndex)"
                        />
                      </template>
                    </svg>
                    <div v-if="videoReady" class="camera-metrics">
                      {{ videoWidth || '—' }} x {{ videoHeight || '—' }}
                    </div>
                    <div v-if="cameraRunning && (streamConnecting || streamError)" class="camera-stream-message">
                      {{ streamError || '正在连接实时视频流…' }}
                    </div>
                  </div>
                  <div class="roi-toolbar">
                    <div>
                      <el-button size="small" :disabled="!totalRoiPointCount" @click="clearRoi">清除ROI</el-button>
                    </div>
                    <span class="roi-help"><el-icon><QuestionFilled /></el-icon>双击画面新建顶点，拖拽调整，右键删除</span>
                    <span class="roi-count">ROI区域: {{ roiAreas.length }} · 顶点: {{ totalRoiPointCount }}</span>
                  </div>
                </div>

                <aside class="camera-info">
                  <h4>摄像头信息</h4>
                  <dl>
                    <dt>运行状态</dt><dd>{{ cameraStateLabel }}</dd>
                    <dt>来源</dt><dd>RK3588 主相机</dd>
                    <dt>配置分辨率</dt><dd>{{ cameraStatus?.resolution || '—' }}</dd>
                    <dt>实际分辨率</dt><dd>{{ videoWidth && videoHeight ? `${videoWidth} x ${videoHeight}` : '—' }}</dd>
                    <dt>传输协议</dt><dd>WebRTC（WHEP）</dd>
                    <dt>流服务</dt><dd>{{ cameraStatus?.mediaMtx === 'running' ? '已连接' : '未连接' }}</dd>
                  </dl>
                  <div class="camera-source-controls">
                    <label>输入源
                      <el-select v-model="inputSource" :disabled="sourceBusy || cameraRunning" size="small" @change="handleInputSourceChange(inputSource)">
                        <el-option label="主相机" value="camera" />
                        <el-option label="服务器录像" value="video" />
                      </el-select>
                    </label>
                    <label v-if="inputSource === 'video'">服务器录像
                      <el-select
                        v-model="localVideoUri"
                        :disabled="sourceBusy || cameraRunning"
                        placeholder="选择已录制视频"
                        size="small"
                        filterable
                        @visible-change="handleRecordingDropdownVisible"
                        @change="handleRecordedVideoChange"
                      >
                        <el-option
                          v-for="item in recordedVideos"
                          :key="item.filename"
                          :label="item.filename"
                          :value="recordingUri(item.filename)"
                        />
                      </el-select>
                    </label>
                    <label v-if="inputSource === 'camera'">启动分辨率
                      <el-select v-model="selectedResolution" :disabled="sourceBusy || cameraRunning" size="small" @change="handleResolutionChange">
                        <el-option label="1920 × 1080" value="1920x1080" />
                        <el-option label="640 × 480" value="640x480" />
                      </el-select>
                    </label>
                  </div>
                  <div class="camera-info-actions">
                    <el-button type="primary" :loading="cameraBusy && !cameraRunning" :disabled="cameraRunning" @click="handleCameraStart">{{ inputSource === 'video' ? '播放录像' : '打开摄像头' }}</el-button>
                    <el-button :loading="cameraBusy && cameraRunning" :disabled="!cameraRunning" @click="handleCameraStop">关闭摄像头</el-button>
                  </div>
                </aside>
              </div>

            </div>
          </section>

          </div>
          <aside class="right-column">
            <SopSummaryCard :sop="activeSop" />
            <section v-if="steps.length" class="parameter-panel panel">
              <h3>步骤参数</h3>
              <div class="parameter-fields">
                <label>步骤名称<el-input v-model="activeStep.name" @input="markChanged" /></label>
                <label>超时时间（秒）<el-input-number v-model="activeStep.timeout" :min="1" controls-position="right" @change="markChanged" /></label>
                <label>相机选择<el-select v-model="activeStep.camera" @change="markChanged">
                    <el-option label="主相机（/dev/video0）" value="主相机（/dev/video0）" />
                    <el-option label="备用相机（/dev/video1）" value="备用相机（/dev/video1）" />
                  </el-select></label>
                <label>手部 ROI<el-select v-model="activeStep.handRoiId" clearable placeholder="不限制手部区域" @change="markChanged">
                    <el-option v-for="roi in roiOptions" :key="roi.value" :label="roi.label" :value="roi.value" />
                  </el-select></label>
                <label>确认帧<el-input-number v-model="activeStep.minConfirmFrames" :min="1" controls-position="right" @change="markChanged" /></label>
                <label>最短驻留（秒）<el-input-number v-model="activeStep.minStageSec" :min="0" :step="0.1" :precision="1" controls-position="right" @change="markChanged" /></label>
                <label>手物距离（米，0=关闭）<el-input-number v-model="activeStep.maxHandObjectDistanceM" :min="0" :step="0.01" :precision="2" controls-position="right" @change="markChanged" /></label>
              </div>
              <p class="confirm-frame-help">连续 {{ activeStep.minConfirmFrames }} 帧检测到对象，判定为成功</p>

              <div class="required-object-section">
                <div class="required-object-heading">
                  <div>
                    <h4>必检对象</h4>
                    <span>{{ activeStep.requiredObjects.length }} 项</span>
                  </div>
                  <el-button type="primary" plain size="small" :icon="Plus" @click="openCreateRequiredObject">新增</el-button>
                </div>
                <div v-if="!activeStep.requiredObjects.length" class="required-object-empty">
                  暂无必检对象
                </div>
                <div v-else class="required-object-list">
                  <div v-for="(object, index) in activeStep.requiredObjects" :key="object.id" class="required-object-item">
                    <div class="required-object-copy">
                      <strong>{{ object.name }} <em>× {{ object.quantity }}</em></strong>
                      <span>{{ object.field }}</span>
                      <small><el-icon><Operation /></el-icon>{{ objectPositionLabel(object) }}</small>
                      <small v-if="objectRelationLabel(object)" class="object-relation-label">
                        <el-icon><Connection /></el-icon>{{ objectRelationLabel(object) }}
                      </small>
                    </div>
                    <div class="required-object-actions">
                      <button type="button" :aria-label="`编辑${object.name}`" @click="openEditRequiredObject(index)"><el-icon><EditPen /></el-icon></button>
                      <button type="button" :aria-label="`删除${object.name}`" @click="deleteRequiredObject(index)"><el-icon><Delete /></el-icon></button>
                    </div>
                  </div>
                </div>
              </div>
            </section>

          </aside>
        </div>

      <el-dialog
        v-model="sopDialogVisible"
        :title="sopDialogMode === 'create' ? '新建 SOP 流程' : '编辑 SOP 流程'"
        width="480px"
        destroy-on-close
      >
        <el-form label-position="top" @submit.prevent="submitSop">
          <el-form-item label="SOP 名称" required>
            <el-input
              v-model="sopFormName"
              maxlength="40"
              show-word-limit
              placeholder="例如：包装质量检测 SOP"
              autofocus
              @keyup.enter="submitSop"
            />
          </el-form-item>
          <el-form-item label="步骤执行顺序" required>
            <el-select
              v-model="sopFormExecutionMode"
              placeholder="请选择有序或无序"
              style="width: 100%"
            >
              <el-option label="有序" value="ordered" />
              <el-option label="无序" value="unordered" />
            </el-select>
            <div class="roi-select-help">
              有序流程按步骤排列顺序执行；无序流程允许步骤以任意顺序完成。
            </div>
          </el-form-item>
          <el-form-item label="流程说明">
            <el-input
              v-model="sopFormDescription"
              type="textarea"
              :rows="4"
              maxlength="160"
              show-word-limit
              placeholder="说明该流程适用的工位、产品或检测目标"
            />
          </el-form-item>
        </el-form>
        <template #footer>
          <el-button @click="sopDialogVisible = false">取消</el-button>
          <el-button type="primary" @click="submitSop">{{ sopDialogMode === "create" ? "创建流程" : "保存修改" }}</el-button>
        </template>
      </el-dialog>

      <el-dialog
        v-model="stepDialogVisible"
        title="添加步骤"
        width="480px"
        destroy-on-close
      >
        <el-form label-position="top" @submit.prevent="submitStep">
          <el-form-item label="步骤名称" required>
            <el-input
              v-model="stepFormName"
              maxlength="40"
              show-word-limit
              placeholder="例如：抓取图像"
              autofocus
            />
          </el-form-item>
          <el-form-item label="步骤描述" required>
            <el-input
              v-model="stepFormDescription"
              type="textarea"
              :rows="4"
              maxlength="160"
              show-word-limit
              placeholder="说明该步骤的操作内容、检测目标或完成条件"
            />
          </el-form-item>
        </el-form>
        <template #footer>
          <el-button @click="stepDialogVisible = false">取消</el-button>
          <el-button type="primary" @click="submitStep">添加步骤</el-button>
        </template>
      </el-dialog>

      <el-dialog
        v-model="requiredObjectDialogVisible"
        :title="requiredObjectDialogMode === 'create' ? '新增必检对象' : '编辑必检对象'"
        width="520px"
        destroy-on-close
      >
        <el-form label-position="top" @submit.prevent="submitRequiredObject">
          <div class="required-object-form-grid">
            <el-form-item label="对象名称" required>
              <el-input v-model="requiredObjectForm.name" maxlength="30" placeholder="例如：轴承" />
            </el-form-item>
            <el-form-item label="字段" required>
              <el-input v-model="requiredObjectForm.field" maxlength="40" placeholder="例如：bearing" />
            </el-form-item>
          </div>
          <el-form-item label="数量" required>
            <el-input-number v-model="requiredObjectForm.quantity" :min="1" :max="999" controls-position="right" />
          </el-form-item>
          <el-form-item label="出现位置（可不选或多选）">
            <div class="object-position-control">
              <el-select
                v-model="requiredObjectForm.roiIds"
                multiple
                clearable
                collapse-tags
                collapse-tags-tooltip
                placeholder="不限位置"
              >
                <el-option v-for="roi in roiOptions" :key="roi.value" :label="roi.label" :value="roi.value" />
              </el-select>
              <el-button type="primary" plain :icon="Plus" @click="beginRequiredObjectRoiSelection">添加ROI</el-button>
            </div>
            <div class="roi-select-help">
              可选择已有区域，或点击“添加ROI”返回主画面标注新区域
            </div>
          </el-form-item>
          <el-form-item label="与其他必检对象的关系（可选）">
            <div class="object-relation-control">
              <el-select
                v-model="requiredObjectForm.relationTargetId"
                clearable
                :disabled="!availableRelationObjects.length"
                placeholder="选择已添加的必检对象"
                @change="handleRelationTargetChange"
              >
                <el-option
                  v-for="object in availableRelationObjects"
                  :key="object.id"
                  :label="object.label"
                  :value="object.id"
                />
              </el-select>
              <el-select
                v-model="requiredObjectForm.relationType"
                :disabled="!requiredObjectForm.relationTargetId"
                placeholder="关系类型"
              >
                <el-option label="重叠" value="overlaps" />
              </el-select>
            </div>
            <div v-if="!availableRelationObjects.length" class="roi-select-help">当前没有可关联的其他必检对象</div>
          </el-form-item>
        </el-form>
        <template #footer>
          <el-button @click="requiredObjectDialogVisible = false">取消</el-button>
          <el-button type="primary" @click="submitRequiredObject">
            {{ requiredObjectDialogMode === 'create' ? '添加对象' : '保存修改' }}
          </el-button>
        </template>
      </el-dialog>
  </section>
</template>
