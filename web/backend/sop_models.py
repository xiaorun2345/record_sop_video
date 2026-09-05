from __future__ import annotations

from typing import Literal, Optional

from pydantic import BaseModel, ConfigDict, Field


class ApiModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class RoiPoint(ApiModel):
    x: float = Field(ge=0, le=1)
    y: float = Field(ge=0, le=1)


class RoiArea(ApiModel):
    id: str = Field(min_length=1)
    name: str = Field(min_length=1, max_length=80)
    points: list[RoiPoint]


class RequiredObjectRelation(ApiModel):
    targetObjectId: str = Field(min_length=1)
    type: Literal["overlaps"]


class RequiredObject(ApiModel):
    id: str = Field(min_length=1)
    name: str = Field(min_length=1, max_length=80)
    field: str = Field(min_length=1, max_length=80)
    quantity: int = Field(ge=1, le=999)
    roiIds: list[str] = Field(default_factory=list)
    relation: Optional[RequiredObjectRelation] = None


class SopStep(ApiModel):
    id: str = Field(min_length=1)
    name: str = Field(min_length=1, max_length=80)
    description: str = Field(default="", max_length=500)
    timeout: float = Field(gt=0)
    enabled: bool = True
    camera: str = Field(min_length=1, max_length=120)
    roiAreas: list[RoiArea] = Field(default_factory=list)
    handRoiId: str = ""
    requiredObjects: list[RequiredObject] = Field(default_factory=list)
    minConfirmFrames: int = Field(ge=1)
    minStageSec: float = Field(default=0.8, ge=0)
    maxHandObjectDistanceM: float = Field(default=0, ge=0)


class SopDefinition(ApiModel):
    id: str = Field(min_length=1)
    name: str = Field(min_length=1, max_length=80)
    description: str = Field(default="", max_length=500)
    executionMode: Literal["ordered", "unordered"]
    version: str
    status: Literal["draft", "published"]
    updatedAt: str
    steps: list[SopStep] = Field(default_factory=list)


class CreateSopInput(ApiModel):
    name: str = Field(min_length=1, max_length=80)
    description: str = Field(default="", max_length=500)
    executionMode: Literal["ordered", "unordered"]


class UpdateSopInput(CreateSopInput):
    pass


class SopValidationIssue(ApiModel):
    path: str
    message: str
