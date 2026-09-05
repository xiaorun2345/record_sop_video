from __future__ import annotations

from datetime import datetime
from typing import Optional
from uuid import uuid4

from .sop_models import CreateSopInput, SopDefinition, SopStep, SopValidationIssue


def now_text() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def create_default_step() -> SopStep:
    return SopStep(
        id="STEP-001",
        name="步骤1",
        description="配置当前步骤的检测目标与执行条件。",
        timeout=3,
        enabled=True,
        camera="主相机（/dev/video0）",
        roiAreas=[],
        requiredObjects=[],
        minConfirmFrames=3,
    )


def create_sop(payload: CreateSopInput) -> SopDefinition:
    return SopDefinition(
        id=f"SOP-{uuid4().hex[:12].upper()}",
        name=payload.name.strip(),
        description=payload.description.strip(),
        executionMode=payload.executionMode,
        version="草稿",
        status="draft",
        updatedAt=now_text(),
        steps=[create_default_step()],
    )


def next_version(version: Optional[str]) -> str:
    if not version or not version.startswith("v"):
        return "v1.0"
    try:
        major, minor = version[1:].split(".", maxsplit=1)
        return f"v{int(major)}.{int(minor) + 1}"
    except (TypeError, ValueError):
        return "v1.0"


def validate_sop(sop: SopDefinition) -> list[SopValidationIssue]:
    issues: list[SopValidationIssue] = []
    if not sop.steps:
        issues.append(SopValidationIssue(path="steps", message="SOP 至少需要一个步骤"))
    step_ids: set[str] = set()
    object_ids = {
        item.id
        for step in sop.steps
        for item in step.requiredObjects
    }
    seen_object_ids: set[str] = set()
    for step_index, step in enumerate(sop.steps):
        path = f"steps[{step_index}]"
        if not step.requiredObjects:
            issues.append(SopValidationIssue(path=f"{path}.requiredObjects", message=f"步骤“{step.name}”至少需要配置一个必检对象，才能生成触发条件"))
        if step.id in step_ids:
            issues.append(SopValidationIssue(path=f"{path}.id", message=f"步骤 ID“{step.id}”重复"))
        step_ids.add(step.id)

        roi_ids = {area.id for area in step.roiAreas}
        if step.handRoiId and step.handRoiId not in roi_ids:
            issues.append(SopValidationIssue(path=f"{path}.handRoiId", message=f"步骤“{step.name}”引用了不存在的手部 ROI"))
        for area in step.roiAreas:
            if len(area.points) < 3:
                issues.append(SopValidationIssue(path=f"{path}.roiAreas.{area.id}", message=f"ROI“{area.name}”至少需要 3 个顶点"))

        fields: set[str] = set()
        for item in step.requiredObjects:
            field = item.field.strip().lower()
            if item.id in seen_object_ids:
                issues.append(SopValidationIssue(path=f"{path}.requiredObjects.{item.id}.id", message=f"必检对象 ID“{item.id}”在当前 SOP 中重复"))
            seen_object_ids.add(item.id)
            if field in fields:
                issues.append(SopValidationIssue(path=f"{path}.requiredObjects.{item.id}.field", message=f"必检对象字段“{item.field}”重复"))
            fields.add(field)
            if any(roi_id not in roi_ids for roi_id in item.roiIds):
                issues.append(SopValidationIssue(path=f"{path}.requiredObjects.{item.id}.roiIds", message=f"必检对象“{item.name}”引用了不存在的 ROI"))
            if item.relation and (item.relation.targetObjectId not in object_ids or item.relation.targetObjectId == item.id):
                issues.append(SopValidationIssue(path=f"{path}.requiredObjects.{item.id}.relation", message=f"必检对象“{item.name}”的关联对象无效"))
    return issues
