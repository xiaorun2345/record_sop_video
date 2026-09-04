from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field
from . import device_api

router = APIRouter(prefix="/api", tags=["system"])
class TimeConfig(BaseModel): timezone: str = "UTC"; ntp_enabled: bool = True; ntp_server: str = "pool.ntp.org"
class StoragePolicy(BaseModel): auto_cleanup_days: int = Field(30, ge=1, le=3650); video_retention_enabled: bool = True; low_space_threshold_percent: int = Field(10, ge=1, le=90)
class ServicesConfig(BaseModel): api_port: int = Field(8080, ge=1024, le=65535); node_red_port: int = Field(1880, ge=1024, le=65535)
@router.get("/system/overview")
def system_overview(): return device_api.overview()
@router.get("/time")
def get_time(): return device_api.overview()["time_config"]
@router.put("/time")
def save_time(value: TimeConfig): return {"time": device_api.put_config(device_api.TIME_FILE, value.model_dump(), {})}
@router.get("/storage-policy")
def get_storage(): return {k: device_api.overview()["storage"][k] for k in ("auto_cleanup_days", "video_retention_enabled", "low_space_threshold_percent")}
@router.put("/storage-policy")
def save_storage(value: StoragePolicy): return {"storage": device_api.put_config(device_api.STORAGE_FILE, value.model_dump(), {})}
@router.get("/services/config")
def get_services(): return {"web_port":8080, **{k: device_api.overview()["services"][k] for k in ("api_port", "node_red_port")}}
@router.put("/services/config")
def save_services(value: ServicesConfig): return {"services": device_api.put_config(device_api.SERVICES_FILE, value.model_dump(), {})}
@router.post("/system/factory-reset")
def factory_reset(value: dict):
    if value.get("confirm") is not True: raise HTTPException(400, "必须二次确认恢复出厂设置")
    if value.get("password") != "admin": raise HTTPException(403, "管理员密码错误")
    return {"ok": True, "message": "恢复出厂请求已登记"}
