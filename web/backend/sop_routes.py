from __future__ import annotations

from pathlib import Path
import json
import time
from threading import RLock

from fastapi import APIRouter, HTTPException, Response, status

from .sop_domain import create_sop, next_version, now_text, validate_sop
from .sop_models import CreateSopInput, SopDefinition, SopValidationIssue, UpdateSopInput
from .sop_repository import SopRepository
from . import sop_runtime

router = APIRouter(prefix="/api", tags=["sop"])
repository = SopRepository(Path(__file__).resolve().parent / "data" / "sop.db")
RUNTIME_STATE_PATH = Path("/tmp/rk3588_sop_runtime_state.json")
RUNTIME_RESET_PATH = Path("/tmp/rk3588_sop_reset")
_runtime_state_lock = RLock()
_runtime_state_cache: dict[str, object] | None = None
_runtime_state_mtime_ns = -1


@router.get("/sops", response_model=list[SopDefinition])
def list_sops() -> list[SopDefinition]:
    return repository.list()


@router.post("/sops", response_model=SopDefinition, status_code=status.HTTP_201_CREATED)
def add_sop(payload: CreateSopInput) -> SopDefinition:
    if any(item.name.strip().lower() == payload.name.strip().lower() for item in repository.list()):
        raise HTTPException(409, "SOP 名称已存在")
    item = create_sop(payload)
    repository.insert(item)
    return item


@router.patch("/sops/{sop_id}", response_model=SopDefinition)
def update_sop(sop_id: str, payload: UpdateSopInput) -> SopDefinition:
    sop = _require(sop_id)
    if any(item.id != sop_id and item.name.strip().lower() == payload.name.strip().lower() for item in repository.list()):
        raise HTTPException(409, "SOP 名称已存在")
    sop.name, sop.description, sop.executionMode = payload.name.strip(), payload.description.strip(), payload.executionMode
    sop.status, sop.version, sop.updatedAt = "draft", "草稿", now_text()
    repository.replace(sop)
    return sop


@router.delete("/sops/{sop_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_sop(sop_id: str) -> Response:
    if repository.count() <= 1:
        raise HTTPException(409, "至少需要保留一个 SOP 流程")
    if not repository.remove(sop_id):
        raise HTTPException(404, "SOP 流程不存在")
    return Response(status_code=204)


@router.post("/sops/{sop_id}/validate", response_model=list[SopValidationIssue])
def validate(sop_id: str, payload: SopDefinition) -> list[SopValidationIssue]:
    _ensure_id(sop_id, payload.id)
    _require(sop_id)
    return validate_sop(payload)


@router.post("/sops/{sop_id}/publish", response_model=SopDefinition)
def publish(sop_id: str, payload: SopDefinition) -> SopDefinition:
    _ensure_id(sop_id, payload.id)
    _require(sop_id)
    issues = validate_sop(payload)
    if issues:
        raise HTTPException(422, detail={"message": issues[0].message, "issues": [item.model_dump() for item in issues]})
    payload.status = "published"
    payload.version = next_version(repository.get_published_version(sop_id))
    payload.updatedAt = now_text()
    repository.replace(payload, update_published_version=True)
    sop_runtime.activate(payload)
    return payload


@router.get("/sop/runtime")
def runtime() -> dict[str, object]:
    payload = sop_runtime.load_active()
    return {"active": payload is not None, "sopId": sop_runtime.active_id(), "judgement": payload}


@router.get("/sop/runtime/state")
def runtime_state() -> dict[str, object]:
    global _runtime_state_cache, _runtime_state_mtime_ns
    payload = sop_runtime.load_active()
    result: dict[str, object] = {"active": payload is not None, "judgement": payload, "available": False,
                                 "state": "idle", "stale": True}
    try:
        stat = RUNTIME_STATE_PATH.stat()
        with _runtime_state_lock:
            if _runtime_state_cache is None or stat.st_mtime_ns != _runtime_state_mtime_ns:
                _runtime_state_cache = json.loads(RUNTIME_STATE_PATH.read_text(encoding="utf-8"))
                _runtime_state_mtime_ns = stat.st_mtime_ns
            snapshot = dict(_runtime_state_cache)
        age = max(0.0, time.time() - stat.st_mtime)
        snapshot["stale"] = age > 2.0
        snapshot["snapshotAgeSec"] = round(age, 3)
        if snapshot["stale"]:
            snapshot["state"] = "stale"
        result.update(snapshot)
        result["available"] = not snapshot["stale"]
    except (OSError, ValueError, TypeError):
        pass
    return result


@router.post("/sop/runtime/reset")
def reset_runtime() -> dict[str, object]:
    global _runtime_state_cache, _runtime_state_mtime_ns
    RUNTIME_RESET_PATH.touch()
    RUNTIME_STATE_PATH.unlink(missing_ok=True)
    with _runtime_state_lock:
        _runtime_state_cache = None
        _runtime_state_mtime_ns = -1
    return {"ok": True, "message": "SOP 运行状态将在下一帧重置"}


@router.get("/sops/{sop_id}/judgement")
def judgement(sop_id: str) -> dict[str, object]:
    sop = _require(sop_id)
    if sop.status != "published":
        raise HTTPException(409, "请先发布 SOP，再读取判定条件")
    return sop_runtime.judgement_config(sop)


def _require(sop_id: str) -> SopDefinition:
    item = repository.get(sop_id)
    if not item:
        raise HTTPException(404, "SOP 流程不存在")
    return item


def _ensure_id(path_id: str, payload_id: str) -> None:
    if path_id != payload_id:
        raise HTTPException(400, "路径中的 SOP ID 与请求数据不一致")
