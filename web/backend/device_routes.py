from fastapi import APIRouter, HTTPException
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field
from pathlib import Path
from . import device_api

router = APIRouter(prefix="/api", tags=["device"])

class Identity(BaseModel):
    device_name: str = Field(min_length=1, max_length=80)
    device_id: str = Field(min_length=1, max_length=80)
    station_id: str = Field(min_length=1, max_length=80)
    hostname: str = Field(min_length=1, max_length=80)

class Network(BaseModel):
    interface: str = "自动选择"; mode: str = "dhcp"; static_ip: str = ""; netmask: str = "255.255.255.0"; gateway: str = ""; dns: str = "8.8.8.8"; wifi_ssid: str = ""; wifi_password: str = ""

@router.get("/device/overview")
def overview(): return device_api.overview()
@router.get("/device/health")
def health(): return device_api.health()
@router.get("/network")
def network(): return device_api._network()
@router.put("/device/identity")
def identity(value: Identity): return {"identity": device_api.put_identity(value.model_dump())}
@router.put("/network")
def save_network(value: Network):
    if value.mode not in ("dhcp", "static"): raise HTTPException(400, "地址模式必须是 dhcp 或 static")
    if value.mode == "static" and not value.static_ip: raise HTTPException(400, "静态 IP 模式必须填写 IP 地址")
    return {"network": device_api.put_config(device_api.NETWORK_FILE, value.model_dump(), {})}
@router.post("/network/test")
def network_test(): return device_api.handle_post("/api/network/test", {})[1]
@router.get("/peripherals/alarm-light")
def alarm(): return device_api.overview()["peripherals"]["alarm_light"]
@router.post("/peripherals/alarm-light/test")
def alarm_test(): return device_api.handle_post("/api/peripherals/alarm-light/test", {})[1]
@router.post("/config/backup")
def backup(): return device_api.backup()
@router.get("/config/backups/{filename}")
def download_backup(filename: str):
    safe = Path(filename).name
    if safe != filename or not safe.startswith("device-config-") or not safe.endswith(".json"):
        raise HTTPException(400, "无效的备份文件名")
    path = device_api.BACKUP_DIR / safe
    if not path.is_file(): raise HTTPException(404, "备份文件不存在")
    return FileResponse(path, media_type="application/json", filename=safe)
@router.post("/config/restore")
def restore(): return device_api.restore_latest()
@router.post("/system/clear-logs")
def clear_logs(): return device_api.handle_post("/api/system/clear-logs", {})[1]
@router.post("/services/restart")
def restart(): return device_api.handle_post("/api/services/restart", {})[1]
