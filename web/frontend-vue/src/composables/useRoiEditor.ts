import { computed, ref, type ComputedRef, type Ref } from "vue";
import { createId } from "@/domain/sop";
import type { RoiArea, RoiPoint, SopStep } from "@/types/sop";

interface RoiEditorOptions {
  activeStep: ComputedRef<SopStep>;
  videoReady: Readonly<Ref<boolean>>;
  videoElement: Ref<HTMLVideoElement | null>;
  onChanged: () => void;
  onSelectionSaved: (roiId: string) => void;
  onWarning: (message: string) => void;
}

const ignoredTargets = "button, input, .camera-controls, .camera-status, .camera-stream-message, .roi-drawing-banner";

export function polygonPoints(points: RoiPoint[], videoWidth: number, videoHeight: number): string {
  const width = videoWidth || 1;
  const height = videoHeight || 1;
  return points.map((point) => `${point.x * width},${point.y * height}`).join(" ");
}

export function useRoiEditor(options: RoiEditorOptions) {
  const activeRoiId = ref("");
  const draggingPoint = ref<{ roiId: string; pointIndex: number } | null>(null);
  const roiSelectionActive = ref(false);
  const pendingRoiId = ref("");

  const roiAreas = computed(() => options.activeStep.value.roiAreas);
  const activeRoi = computed(() => {
    return roiAreas.value.find((area) => area.id === activeRoiId.value) ?? roiAreas.value.at(-1);
  });
  const totalRoiPointCount = computed(() => roiAreas.value.reduce((total, area) => total + area.points.length, 0));
  const roiOptions = computed(() => roiAreas.value
    .filter((area) => area.points.length >= 3)
    .map((area) => ({ label: area.name, value: area.id })));
  const pendingRoiPointCount = computed(() => {
    return roiAreas.value.find((area) => area.id === pendingRoiId.value)?.points.length ?? 0;
  });

  function createRoiArea(): RoiArea {
    const area: RoiArea = {
      id: createId("ROI"),
      name: `ROI区域 ${roiAreas.value.length + 1}`,
      points: [],
    };
    roiAreas.value.push(area);
    activeRoiId.value = area.id;
    return area;
  }

  function beginSelection() {
    roiSelectionActive.value = true;
    pendingRoiId.value = createRoiArea().id;
  }

  function cancelSelection() {
    const index = roiAreas.value.findIndex((area) => area.id === pendingRoiId.value);
    if (index >= 0) roiAreas.value.splice(index, 1);
    activeRoiId.value = roiAreas.value.at(-1)?.id ?? "";
    pendingRoiId.value = "";
    roiSelectionActive.value = false;
  }

  function handleFrameContextMenu(event: MouseEvent) {
    if (!roiSelectionActive.value) return;
    event.preventDefault();
    const target = event.target as Element;
    if (target.closest(ignoredTargets)) return;
    const area = roiAreas.value.find((item) => item.id === pendingRoiId.value);
    if (!area || area.points.length < 3) {
      options.onWarning("请至少添加 3 个顶点后再保存 ROI 区域");
      return;
    }
    const position = eventPosition(event);
    if (!position) {
      options.onWarning("请在视频有效画面内点击鼠标右键保存");
      return;
    }
    if (!pointInsidePolygon(position, area.points)) {
      options.onWarning("请在 ROI 多边形内部点击鼠标右键保存");
      return;
    }
    pendingRoiId.value = "";
    roiSelectionActive.value = false;
    options.onSelectionSaved(area.id);
    options.onChanged();
  }

  function startPointDrag(roiId: string, pointIndex: number, event: PointerEvent) {
    if (event.button !== 0 || (roiSelectionActive.value && roiId !== pendingRoiId.value)) return;
    activeRoiId.value = roiId;
    draggingPoint.value = { roiId, pointIndex };
    (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
  }

  function addRoiPoint(event: MouseEvent) {
    if (!options.videoReady.value || (event.target as Element).closest(ignoredTargets)) return;
    const position = eventPosition(event);
    if (!position) {
      options.onWarning("请在视频有效画面内添加 ROI 顶点");
      return;
    }
    const pendingArea = roiSelectionActive.value
      ? roiAreas.value.find((item) => item.id === pendingRoiId.value)
      : undefined;
    const area = pendingArea ?? (roiSelectionActive.value ? createRoiArea() : activeRoi.value ?? createRoiArea());
    if (roiSelectionActive.value) pendingRoiId.value = area.id;
    activeRoiId.value = area.id;
    area.points.push(position);
    options.onChanged();
  }

  function removeRoiPoint(roiId: string, pointIndex: number) {
    if (roiSelectionActive.value && roiId !== pendingRoiId.value) return;
    const areaIndex = roiAreas.value.findIndex((area) => area.id === roiId);
    if (areaIndex < 0) return;
    const area = roiAreas.value[areaIndex];
    area.points.splice(pointIndex, 1);
    if (area.points.length < 3) {
      options.activeStep.value.requiredObjects.forEach((object) => {
        object.roiIds = object.roiIds.filter((id) => id !== roiId);
      });
    }
    if (!area.points.length) {
      roiAreas.value.splice(areaIndex, 1);
      activeRoiId.value = roiAreas.value.at(-1)?.id ?? "";
    }
    options.onChanged();
  }

  function movePoint(event: PointerEvent) {
    if (!draggingPoint.value) return;
    const area = roiAreas.value.find((item) => item.id === draggingPoint.value?.roiId);
    if (!area) return;
    const position = eventPosition(event, true);
    if (!position) return;
    area.points[draggingPoint.value.pointIndex] = position;
    options.onChanged();
  }

  function stopPointDrag() {
    draggingPoint.value = null;
  }

  function clearRoi() {
    roiAreas.value.splice(0);
    options.activeStep.value.requiredObjects.forEach((object) => {
      object.roiIds = [];
    });
    activeRoiId.value = "";
    pendingRoiId.value = "";
    roiSelectionActive.value = false;
    options.onChanged();
  }

  function eventPosition(event: MouseEvent | PointerEvent, clampOutside = false): RoiPoint | null {
    const video = options.videoElement.value;
    if (!video?.videoWidth || !video.videoHeight) return null;
    const elementRect = video.getBoundingClientRect();
    const intrinsicRatio = video.videoWidth / video.videoHeight;
    const elementRatio = elementRect.width / elementRect.height;
    let contentWidth = elementRect.width;
    let contentHeight = elementRect.height;
    let contentLeft = elementRect.left;
    let contentTop = elementRect.top;

    if (intrinsicRatio > elementRatio) {
      contentHeight = contentWidth / intrinsicRatio;
      contentTop += (elementRect.height - contentHeight) / 2;
    } else {
      contentWidth = contentHeight * intrinsicRatio;
      contentLeft += (elementRect.width - contentWidth) / 2;
    }

    const point = {
      x: (event.clientX - contentLeft) / contentWidth,
      y: (event.clientY - contentTop) / contentHeight,
    };
    const outside = point.x < 0 || point.x > 1 || point.y < 0 || point.y > 1;
    if (outside && !clampOutside) return null;
    return clampPosition(point);
  }

  return {
    roiAreas,
    activeRoi,
    activeRoiId,
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
    clearRoi,
  };
}

function clampPosition(point: RoiPoint): RoiPoint {
  return {
    x: Math.max(0, Math.min(1, point.x)),
    y: Math.max(0, Math.min(1, point.y)),
  };
}

function pointInsidePolygon(point: RoiPoint, vertices: RoiPoint[]): boolean {
  let inside = false;
  for (let current = 0, previous = vertices.length - 1; current < vertices.length; previous = current++) {
    const currentPoint = vertices[current];
    const previousPoint = vertices[previous];
    const intersects = currentPoint.y > point.y !== previousPoint.y > point.y
      && point.x < ((previousPoint.x - currentPoint.x) * (point.y - currentPoint.y))
        / (previousPoint.y - currentPoint.y) + currentPoint.x;
    if (intersects) inside = !inside;
  }
  return inside;
}
