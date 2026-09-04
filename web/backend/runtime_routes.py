from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from fastapi.responses import FileResponse
from pathlib import Path
from . import backend

router = APIRouter(prefix="/api", tags=["runtime"])
class Start(BaseModel): resolution: str = "640x480"
class Recording(BaseModel): mode: str = "processed"
def _recording_file(filename: str) -> Path:
    safe = Path(filename).name
    if safe != filename or Path(safe).suffix.lower() not in (".mp4", ".avi", ".mkv"): raise HTTPException(400, "无效的视频文件名")
    path = backend.RECORDING_DIR / safe
    if not path.is_file(): raise HTTPException(404, "视频文件不存在")
    return path
@router.get("/recordings")
def recordings():
    files=[]
    for path in sorted(backend.RECORDING_DIR.glob("*"), key=lambda p:p.stat().st_mtime, reverse=True) if backend.RECORDING_DIR.exists() else []:
        if path.suffix.lower() in (".mp4", ".avi", ".mkv"):
            stat=path.stat(); files.append({"filename":path.name,"size_bytes":stat.st_size,"size_mb":round(stat.st_size/1048576,2),"modified_at":stat.st_mtime})
    return {"items":files}
@router.get("/recordings/{filename}/download")
def download_recording(filename: str):
    path=_recording_file(filename); return FileResponse(path, filename=path.name, media_type="video/mp4")
@router.delete("/recordings/{filename}")
def delete_recording(filename: str):
    path=_recording_file(filename); path.unlink(); return {"ok":True,"filename":path.name,"message":"视频已删除"}
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
