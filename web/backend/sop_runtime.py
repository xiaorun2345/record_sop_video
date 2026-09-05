"""Shared SOP activation and judgement export for the single edge runtime.

The SOP editor persists rich JSON, while the C++ vision process consumes the
legacy key=value file.  This module is the one translation boundary between
the two formats.  It never opens a camera or starts a second server.
"""
from __future__ import annotations

import json
import os
import re
from pathlib import Path
from typing import Any

from .sop_models import SopDefinition

ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "config" / "sop_config.txt"
ACTIVE_PATH = ROOT / "config" / "active_sop_judgement.json"


def judgement_config(sop: SopDefinition) -> dict[str, Any]:
    return {
        "schemaVersion": "1.2",
        "roiCoordinateSystem": {
            "space": "original-video", "unit": "normalized", "origin": "top-left",
            "xDirection": "right", "yDirection": "down", "range": [0, 1],
        },
        "sop": {
            "id": sop.id, "name": sop.name, "description": sop.description,
            "version": sop.version, "executionMode": sop.executionMode,
            "publishedAt": sop.updatedAt,
        },
        "steps": [dict(order=index + 1, **step.model_dump(mode="json")) for index, step in enumerate(sop.steps)],
    }


def activate(sop: SopDefinition) -> dict[str, Any]:
    """Persist the published snapshot and translate it for the C++ process."""
    payload = judgement_config(sop)
    ACTIVE_PATH.parent.mkdir(parents=True, exist_ok=True)
    ACTIVE_PATH.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    _write_cpp_config(sop)
    return payload


def load_active() -> dict[str, Any] | None:
    try:
        return json.loads(ACTIVE_PATH.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def active_id() -> str | None:
    payload = load_active()
    return str(payload.get("sop", {}).get("id")) if payload else None


def _write_cpp_config(sop: SopDefinition) -> None:
    if not CONFIG_PATH.exists():
        return
    text = CONFIG_PATH.read_text(encoding="utf-8")
    width = _number(text, "input.width", 640)
    height = _number(text, "input.height", 480)
    # Remove generated sections from a previous publish, retaining model and
    # device settings maintained by the existing configuration editor.
    lines = [line for line in text.splitlines() if not line.startswith("sop.generated=")
             and not line.startswith("sop.execution_mode=")
             and not re.match(r"^(roi|step)\.\d+=", line)]
    lines.extend(["", "# Generated from the published SOP configuration.", "sop.generated=true",
                  f"sop.execution_mode={sop.executionMode}"])
    roi_index = 0
    for step in sop.steps:
        for area in step.roiAreas:
            points = ";".join(f"{round(point.x * width)},{round(point.y * height)}" for point in area.points)
            if len(area.points) >= 3:
                lines.append(f"roi.{roi_index}={_safe(area.name)}:{points}")
                roi_index += 1
    for index, step in enumerate(sop.steps):
        objects = ",".join(_object_token(item, step) for item in step.requiredObjects)
        if not objects:
            # The C++ parser requires a non-empty object list.  Keep a harmless
            # impossible label so the process remains valid and never advances.
            objects = "__sop_no_object__:1"
        roi_name = next((area.name for area in step.roiAreas if area.id == step.handRoiId), "")
        warning = f"{_safe(step.name)}未满足触发条件"
        lines.append("step.%d=%s|%s|%s|%s|%d|%g|%s|%g|%g|%s" % (
            index, _safe(step.id), _safe(step.name), objects, _safe(roi_name),
            max(1, int(step.minConfirmFrames)), float(step.timeout), warning,
            float(step.maxHandObjectDistanceM), float(step.minStageSec),
            "1" if step.enabled else "0",
        ))
    temporary = CONFIG_PATH.with_suffix(CONFIG_PATH.suffix + ".tmp")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    os.replace(temporary, CONFIG_PATH)


def _number(text: str, key: str, fallback: int) -> int:
    match = re.search(rf"(?m)^{re.escape(key)}=(\d+)$", text)
    return int(match.group(1)) if match else fallback


def _safe(value: str) -> str:
    return (value.replace("|", "/").replace("=", ":").replace("\n", " ")
            .replace(",", "/").replace("~", "/").replace("+", "/").strip())


def _object_token(item: Any, step: Any) -> str:
    roi_by_id = {area.id: area.name for area in step.roiAreas}
    rois = "+".join(_safe(roi_by_id.get(value, value)) for value in item.roiIds) or "-"
    relation_target = _safe(item.relation.targetObjectId) if item.relation else "-"
    relation_type = _safe(item.relation.type) if item.relation else "-"
    return "~".join((_safe(item.id), _safe(item.field), str(int(item.quantity)), rois,
                     relation_target, relation_type))
