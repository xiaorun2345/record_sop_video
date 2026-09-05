"""Model-management API for the edge runtime."""
from __future__ import annotations

import json
import os
import re
import shutil
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "config" / "sop_config.txt"
MODELS_DIR = ROOT / "models"
BACKUP_DIR = ROOT / "output" / "backups"

MODEL_KEYS = {
    "detector": [
        "detector.backend", "detector.model_path", "detector.labels",
        "detector.conf_threshold", "detector.iou_threshold", "detector.input_size",
    ],
    "hand": [
        "hand.backend", "hand.model_path", "hand.max_num_hands",
        "hand.min_detection_confidence", "hand.min_tracking_confidence",
    ],
    "landmark": [
        "hand.landmark_model_path", "hand.min_tracking_confidence",
        "hand.constraint.enabled", "hand.constraint.calibration_frames",
        "hand.constraint.smoothing", "hand.constraint.max_prediction_frames",
    ],
}


def overview() -> dict[str, Any]:
    config = _read_config()
    files = _model_files()
    detector_files = [
        item for item in files
        if item["path"] not in (config.get("hand.model_path"), config.get("hand.landmark_model_path"))
        and "hand" not in item["name"].lower()
    ]
    return {
        "models": [
            _model("detector", "YOLOv8 目标检测", "可上传和切换", config, detector_files, True),
            _model("hand", "手部检测", "固定模型，仅配置阈值", config, files, False),
            _model("landmark", "手部关键点", "固定模型，仅配置阈值", config, files, False),
        ],
        "files": files,
        "updated_at": time.strftime("%Y-%m-%d %H:%M:%S %Z"),
    }


def save_config(payload: dict[str, Any]) -> dict[str, Any]:
    values = _sanitize(payload)
    _backup_config()
    config = _read_config()
    config.update(values)
    _write_config(config)
    return overview()


def activate_detector_model(path: str) -> dict[str, Any]:
    safe = _safe_model_path(path)
    if not (ROOT / safe).is_file():
        raise RuntimeError("模型文件不存在")
    return save_config({"detector.model_path": safe})


def upload_detector_model(filename: str, body: bytes) -> dict[str, Any]:
    if not body:
        raise RuntimeError("模型文件为空")
    name = Path(filename).name
    if not name.endswith(".rknn"):
        raise RuntimeError("仅支持上传 .rknn 模型")
    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    target = MODELS_DIR / f"yolov8-{timestamp}-{name}"
    target.write_bytes(body)
    rel = str(target.relative_to(ROOT))
    return {"file": _file_entry(target), "activated": activate_detector_model(rel)}


def _model(kind: str, name: str, mode: str, config: dict[str, str], files: list[dict[str, Any]], switchable: bool) -> dict[str, Any]:
    path_key = "detector.model_path" if kind == "detector" else ("hand.model_path" if kind == "hand" else "hand.landmark_model_path")
    active_path = config.get(path_key, "")
    return {
        "kind": kind,
        "name": name,
        "mode": mode,
        "switchable": switchable,
        "active_path": active_path,
        "exists": bool(active_path and (ROOT / active_path).is_file()),
        "config": {key: config.get(key, "") for key in MODEL_KEYS[kind]},
        "available_files": files if kind == "detector" else [item for item in files if item["path"] == active_path],
    }


def _read_config() -> dict[str, str]:
    config: dict[str, str] = {}
    try:
        for line in CONFIG_PATH.read_text(encoding="utf-8").splitlines():
            if not line or line.lstrip().startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            config[key.strip()] = value.strip()
    except OSError:
        pass
    return config


def _write_config(values: dict[str, str]) -> None:
    text = CONFIG_PATH.read_text(encoding="utf-8") if CONFIG_PATH.exists() else ""
    used = set()
    lines = []
    for line in text.splitlines():
        if not line or line.lstrip().startswith("#") or "=" not in line:
            lines.append(line)
            continue
        key = line.split("=", 1)[0].strip()
        if key in values:
            lines.append(f"{key}={values[key]}")
            used.add(key)
        else:
            lines.append(line)
    for key in values:
        if key not in used:
            lines.append(f"{key}={values[key]}")
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    temporary = CONFIG_PATH.with_suffix(CONFIG_PATH.suffix + ".tmp")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    os.replace(temporary, CONFIG_PATH)


def _sanitize(payload: dict[str, Any]) -> dict[str, str]:
    allowed = {key for keys in MODEL_KEYS.values() for key in keys}
    values = {str(key): value for key, value in payload.items() if str(key) in allowed}
    for key in ("hand.backend", "hand.model_path", "hand.landmark_model_path"):
        values.pop(key, None)
    if "detector.model_path" in values:
        values["detector.model_path"] = _safe_model_path(str(values["detector.model_path"]))
    if "detector.backend" in values and str(values["detector.backend"]) != "rknn":
        raise RuntimeError("RK3588 当前仅支持 rknn 后端")
    for key in ("detector.conf_threshold", "detector.iou_threshold", "hand.min_detection_confidence", "hand.min_tracking_confidence", "hand.constraint.smoothing"):
        if key in values:
            values[key] = _range_float(key, values[key], 0, 1)
    for key in ("detector.input_size", "hand.input_size", "hand.max_num_hands", "hand.constraint.calibration_frames", "hand.constraint.max_prediction_frames"):
        if key in values:
            values[key] = _range_int(key, values[key], 0, 4096)
    if "hand.constraint.enabled" in values:
        values["hand.constraint.enabled"] = "true" if str(values["hand.constraint.enabled"]).lower() in ("1", "true", "yes", "on") else "false"
    if "detector.labels" in values:
        labels = [item.strip() for item in str(values["detector.labels"]).split(",") if item.strip()]
        if not labels:
            raise RuntimeError("目标类别不能为空")
        values["detector.labels"] = ",".join(labels)
    return {key: str(value) for key, value in values.items()}


def _safe_model_path(path: str) -> str:
    rel = path.strip().lstrip("/")
    target = (ROOT / rel).resolve()
    if MODELS_DIR.resolve() not in target.parents:
        raise RuntimeError("模型路径必须位于 models 目录")
    if target.suffix != ".rknn":
        raise RuntimeError("仅支持 .rknn 模型")
    return str(target.relative_to(ROOT))


def _range_float(key: str, value: Any, min_value: float, max_value: float) -> str:
    try:
        number = float(value)
    except (TypeError, ValueError):
        raise RuntimeError(f"{key} 必须是数字")
    if number < min_value or number > max_value:
        raise RuntimeError(f"{key} 必须在 {min_value} 到 {max_value} 之间")
    return f"{number:g}"


def _range_int(key: str, value: Any, min_value: int, max_value: int) -> str:
    try:
        number = int(value)
    except (TypeError, ValueError):
        raise RuntimeError(f"{key} 必须是整数")
    if number < min_value or number > max_value:
        raise RuntimeError(f"{key} 必须在 {min_value} 到 {max_value} 之间")
    return str(number)


def _model_files() -> list[dict[str, Any]]:
    if not MODELS_DIR.exists():
        return []
    return [_file_entry(path) for path in sorted(MODELS_DIR.glob("*.rknn"))]


def _file_entry(path: Path) -> dict[str, Any]:
    stat = path.stat()
    return {
        "name": path.name,
        "path": str(path.relative_to(ROOT)),
        "size_mb": round(stat.st_size / 1048576, 2),
        "modified_at": stat.st_mtime,
    }


def _backup_config() -> None:
    if not CONFIG_PATH.exists():
        return
    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    shutil.copy2(CONFIG_PATH, BACKUP_DIR / f"sop_config-models-{time.strftime('%Y%m%d-%H%M%S')}.txt")
