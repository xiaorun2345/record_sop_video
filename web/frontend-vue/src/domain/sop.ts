import type { SopDefinition, SopExecutionMode, SopJudgementConfig, SopStep } from "@/types/sop";

export function createId(prefix: string): string {
  const random = Math.random().toString(36).slice(2, 8).toUpperCase();
  return `${prefix}-${Date.now()}-${random}`;
}

export function formatUpdatedAt(date = new Date()): string {
  return date.toLocaleString("zh-CN", { hour12: false }).replaceAll("/", "-");
}

export function createSopStep(id: string, name: string, overrides: Partial<SopStep> = {}): SopStep {
  return {
    id,
    name,
    description: "配置当前步骤的检测目标与执行条件。",
    timeout: 3,
    enabled: true,
    camera: "主相机（/dev/video0）",
    roiAreas: [],
    handRoiId: "",
    requiredObjects: [],
    minConfirmFrames: 3,
    minStageSec: 0.8,
    maxHandObjectDistanceM: 0,
    ...overrides,
  };
}

export function createDefaultSopStep(): SopStep {
  return createSopStep("STEP-001", "步骤1");
}

export function createSopDefinition(
  name: string,
  description: string,
  executionMode: SopExecutionMode,
): SopDefinition {
  return {
    id: createId("SOP"),
    name,
    description,
    executionMode,
    version: "草稿",
    status: "draft",
    updatedAt: formatUpdatedAt(),
    steps: [createDefaultSopStep()],
  };
}

export function cloneSop<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}

export function createSopJudgementConfig(sop: SopDefinition): SopJudgementConfig {
  return {
    schemaVersion: "1.2",
    roiCoordinateSystem: {
      space: "original-video",
      unit: "normalized",
      origin: "top-left",
      xDirection: "right",
      yDirection: "down",
      range: [0, 1],
    },
    sop: {
      id: sop.id,
      name: sop.name,
      description: sop.description,
      version: sop.version,
      executionMode: sop.executionMode,
      publishedAt: sop.updatedAt,
    },
    steps: sop.steps.map((step, index) => ({
      order: index + 1,
      ...cloneSop(step),
    })),
  };
}
