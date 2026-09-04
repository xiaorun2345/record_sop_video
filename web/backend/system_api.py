"""System-settings API. Dangerous operations are intentionally confirmation-gated."""
try:
    from .device_api import overview, put_identity
except ImportError:
    from device_api import overview, put_identity

def handle_get(path):
    if path == "/api/system/overview": return 200, overview()
    if path == "/api/time": return 200, overview()["time_config"]
    if path == "/api/storage-policy": return 200, {k: overview()["storage"][k] for k in ("auto_cleanup_days", "video_retention_enabled", "low_space_threshold_percent")}
    if path == "/api/services/config": return 200, {"web_port": 8080, **{k: overview()["services"][k] for k in ("api_port", "node_red_port")}}
    return None

def handle_post(path, payload):
    if path == "/api/system/factory-reset":
        if payload.get("confirm") is not True: return 400, {"error": "必须二次确认恢复出厂设置"}
        if payload.get("password") != "admin": return 403, {"error": "管理员密码错误"}
        return 200, {"ok": True, "message": "恢复出厂请求已登记"}
    return None
