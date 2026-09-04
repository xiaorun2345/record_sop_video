from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from . import backend

router = APIRouter(prefix="/api", tags=["runtime"])
class Start(BaseModel): resolution: str = "640x480"
class Recording(BaseModel): mode: str = "processed"
@router.get("/algorithm/status")
@router.get("/recording/status")
def status():
    with backend.lock: return backend.status_locked()
@router.post("/camera/start")
def camera_start(value: Start = Start()):
    with backend.lock:
        backend.set_resolution(value.resolution); backend.camera_start_locked(); return backend.status_locked()
@router.post("/camera/stop")
def camera_stop():
    with backend.lock: backend.camera_stop_locked(); return backend.status_locked()
@router.post("/algorithm/start")
def algorithm_start(value: Start = Start()):
    with backend.lock:
        backend.set_resolution(value.resolution); backend.algorithm_start_locked(); return backend.status_locked()
@router.post("/algorithm/stop")
def algorithm_stop():
    with backend.lock: backend.algorithm_stop_locked(); return backend.status_locked()
@router.post("/visualization/hand-landmarks")
def landmarks(value: dict):
    enabled=value.get("enabled")
    if not isinstance(enabled,bool): raise HTTPException(400,"enabled 必须是布尔值")
    if enabled: backend.HAND_LANDMARKS_VISIBILITY.touch()
    else: backend.HAND_LANDMARKS_VISIBILITY.unlink(missing_ok=True)
    return backend.status_locked()
@router.post("/recording/start")
def recording_start(value: Recording = Recording()):
    if value.mode not in ("processed","raw"): raise HTTPException(400,"录像模式必须是 processed 或 raw")
    with backend.lock:
        backend.start_locked(); backend.RECORDING_DIR.mkdir(parents=True,exist_ok=True); backend.RECORDING_CONTROL.write_text(value.mode); return backend.status_locked()
@router.post("/recording/stop")
def recording_stop():
    with backend.lock: backend.RECORDING_CONTROL.unlink(missing_ok=True); return backend.status_locked()
