import os, shutil, subprocess, time
from datetime import datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError
from fastapi import APIRouter, HTTPException, Query
from pydantic import BaseModel, Field
from . import backend, device_api

router = APIRouter(prefix="/api", tags=["system"])
class TimeConfig(BaseModel): timezone: str = "UTC"; ntp_enabled: bool = True; ntp_server: str = "pool.ntp.org"
class StoragePolicy(BaseModel): auto_cleanup_days: int = Field(30, ge=1, le=3650); low_space_threshold_percent: int = Field(10, ge=1, le=90)
class CleanupRequest(BaseModel): days: int = Field(30, ge=1, le=3650)

def _command(args):
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=8, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return None

def _timedatectl():
    result = _command(["timedatectl", "show", "--no-pager", "--property=Timezone", "--property=SystemClockSynchronized", "--property=NTPSynchronized", "--property=NTPService", "--property=TimeUSec"])
    values = {}
    if result and result.returncode == 0:
        for line in result.stdout.splitlines():
            key, _, value = line.partition("=")
            if key:
                values[key] = value
    return values

def _time_state():
    configured = device_api._json(device_api.TIME_FILE, {"timezone": "UTC", "ntp_enabled": True, "ntp_server": "pool.ntp.org"})
    actual = _timedatectl()
    return {
        **configured,
        "system_time": time.strftime("%Y-%m-%d %H:%M:%S %Z"),
        "timezone_actual": actual.get("Timezone") or time.tzname[0],
        "clock_synchronized": actual.get("SystemClockSynchronized") == "yes" or actual.get("NTPSynchronized") == "yes",
        "ntp_synchronized": actual.get("NTPSynchronized") == "yes",
        "ntp_service": actual.get("NTPService") or "unknown",
        "timedatectl_available": shutil.which("timedatectl") is not None,
    }

def _apply_time(config):
    messages = []
    applied = True
    if shutil.which("timedatectl") is None:
        return False, ["系统没有 timedatectl，未能应用系统时间设置"]
    timezone_result = _command(["timedatectl", "set-timezone", config.timezone])
    if not timezone_result or timezone_result.returncode != 0:
        applied = False
        messages.append("时区未能应用，可能需要系统管理员权限")
    ntp_result = _command(["timedatectl", "set-ntp", "true" if config.ntp_enabled else "false"])
    if not ntp_result or ntp_result.returncode != 0:
        applied = False
        messages.append("NTP 开关未能应用，可能没有可用的系统时间服务")
    if applied:
        messages.append("系统时间设置已应用")
    return applied, messages

def _recent_errors(limit=8):
    lines = []
    for path in (device_api.LOG_DIR / "algorithm.log", device_api.LOG_DIR / "mediamtx.log", device_api.LOG_DIR / "web-backend.log"):
        try:
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines()[-500:]:
                lower = line.lower()
                if any(token in lower for token in ("error", "failed", "timeout", "失败", "异常", "无法", "退出")):
                    lines.append({"source": path.name, "message": line.strip()})
        except OSError:
            continue
    return lines[-limit:]

def _service_status():
    runtime = backend.status_locked()
    configured = device_api._json(device_api.SERVICES_FILE, {"api_port": 8080, "node_red_port": 1880})
    return {
        "backend": {"status": "运行中", "pid": os.getpid(), "port": backend.PORT, "configured_port": configured.get("api_port", 8080)},
        "camera": {"status": runtime["camera"], "pid": runtime.get("cameraPid"), "raw_stream_ready": runtime.get("raw_stream_ready", False)},
        "algorithm": {"status": runtime["algorithm"], "pid": runtime.get("algorithm_pid"), "stream_ready": runtime.get("stream_ready", False)},
        "mediamtx": {"status": runtime["mediamtx"], "rtmp_port": 1935, "webrtc_port": runtime.get("webrtcPort", 8889)},
        "node_red": {"status": "运行中" if backend.port_open(int(configured.get("node_red_port", 1880))) else "未运行", "port": configured.get("node_red_port", 1880)},
    }
@router.get("/system/overview")
def system_overview(): return device_api.overview()
@router.get("/time")
def get_time(): return _time_state()
@router.put("/time")
def save_time(value: TimeConfig):
    try:
        ZoneInfo(value.timezone)
    except (ZoneInfoNotFoundError, ValueError):
        raise HTTPException(400, "无效的时区")
    saved = device_api.put_config(device_api.TIME_FILE, value.model_dump(), {})
    applied, messages = _apply_time(value)
    return {"time": {**saved, **_time_state(), "applied": applied, "message": "；".join(messages)}}
@router.post("/time/sync")
def sync_time():
    state = _time_state()
    if state["ntp_enabled"]:
        result = _command(["timedatectl", "set-ntp", "true"])
        if not result or result.returncode != 0:
            return {"ok": False, "message": "NTP 同步请求失败，可能需要系统管理员权限", "time": _time_state()}
    sync = _command(["chronyc", "makestep"]) if shutil.which("chronyc") else None
    if sync and sync.returncode != 0:
        return {"ok": False, "message": "时间服务已启用，但立即校时失败", "time": _time_state()}
    return {"ok": True, "message": "已发起时间同步，请稍后重新读取状态", "time": _time_state()}
@router.get("/storage-policy")
def get_storage(): return {k: device_api.storage_overview()["policy"][k] for k in ("auto_cleanup_days", "low_space_threshold_percent")}
@router.put("/storage-policy")
def save_storage(value: StoragePolicy): return {"storage": device_api.put_config(device_api.STORAGE_FILE, value.model_dump(), {})}
@router.get("/storage")
def get_storage_overview(): return device_api.storage_overview()
@router.get("/storage/cleanup/preview")
def cleanup_preview(days: int = Query(30, ge=1, le=3650)):
    items = device_api.recordings_before(days)
    return {"days": days, "items": items, "count": len(items), "size_bytes": sum(item["size_bytes"] for item in items)}
@router.post("/storage/cleanup")
def cleanup(value: CleanupRequest):
    items = device_api.cleanup_recordings(value.days)
    return {"ok": True, "days": value.days, "removed": items, "count": len(items), "message": f"已清理 {len(items)} 个过期录像"}
@router.get("/services/config")
def get_services(): return {"web_port": backend.PORT, "api_port": backend.PORT, "node_red_port": device_api._json(device_api.SERVICES_FILE, {"node_red_port": 1880}).get("node_red_port", 1880)}
@router.put("/services/config")
def save_services(value: dict): raise HTTPException(410, "服务端口由启动配置管理，系统设置页面不提供在线修改")
@router.get("/system/status")
def system_status():
    with backend.lock:
        return {"updated_at": time.strftime("%Y-%m-%d %H:%M:%S %Z"), "services": _service_status(), "errors": _recent_errors()}
@router.get("/system/diagnostics")
def diagnostics():
    with backend.lock:
        return {"generated_at": datetime.now(timezone.utc).isoformat(), "time": _time_state(), "system": device_api.health(), "services": _service_status(), "errors": _recent_errors(30)}
@router.post("/system/factory-reset")
def factory_reset(value: dict):
    if value.get("confirm") is not True: raise HTTPException(400, "必须二次确认恢复出厂设置")
    if value.get("password") != "admin": raise HTTPException(403, "管理员密码错误")
    return {"ok": True, "message": "恢复出厂请求已登记"}
