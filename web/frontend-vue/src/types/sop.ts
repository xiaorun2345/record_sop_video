export type SopStatus = "draft" | "published";
export type SopExecutionMode = "ordered" | "unordered";
export type RequiredObjectRelationType = "overlaps";

export interface RoiPoint {
  x: number;
  y: number;
}

export interface RoiArea {
  id: string;
  name: string;
  points: RoiPoint[];
}

export interface RequiredObjectRelation {
  targetObjectId: string;
  type: RequiredObjectRelationType;
}

export interface RequiredObject {
  id: string;
  name: string;
  field: string;
  quantity: number;
  roiIds: string[];
  relation: RequiredObjectRelation | null;
}

export interface SopStep {
  id: string;
  name: string;
  description: string;
  timeout: number;
  enabled: boolean;
  camera: string;
  roiAreas: RoiArea[];
  handRoiId: string;
  requiredObjects: RequiredObject[];
  minConfirmFrames: number;
  minStageSec: number;
  maxHandObjectDistanceM: number;
}

export interface SopDefinition {
  id: string;
  name: string;
  description: string;
  executionMode: SopExecutionMode;
  version: string;
  status: SopStatus;
  updatedAt: string;
  steps: SopStep[];
}

export interface SopJudgementStep extends SopStep {
  order: number;
}

export interface SopJudgementConfig {
  schemaVersion: "1.2";
  roiCoordinateSystem: {
    space: "original-video";
    unit: "normalized";
    origin: "top-left";
    xDirection: "right";
    yDirection: "down";
    range: [0, 1];
  };
  sop: {
    id: string;
    name: string;
    description: string;
    version: string;
    executionMode: SopExecutionMode;
    publishedAt: string;
  };
  steps: SopJudgementStep[];
}

export interface CreateSopInput {
  name: string;
  description: string;
  executionMode: SopExecutionMode;
}

export interface UpdateSopInput extends CreateSopInput {
  id: string;
}

export interface SopValidationIssue {
  path: string;
  message: string;
}
