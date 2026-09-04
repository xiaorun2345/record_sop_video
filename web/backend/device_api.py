"""Device-management API kept separate from the runtime algorithm API."""
import json, shutil, socket, time, urllib.request, os, termios
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IDENTITY_FILE = ROOT / "config" / "device_identity.json"
BACKUP_DIR = ROOT / "output" / "backups"
NETWORK_FILE = ROOT / "config" / "network.json"
TIME_FILE = ROOT / "config" / "time.json"
STORAGE_FILE = ROOT / "config" / "storage_policy.json"
SERVICES_FILE = ROOT / "config" / "services.json"
def _json(path, defaults):
    try: defaults.update(json.loads(path.read_text(encoding="utf-8")))
    except (OSError, ValueError): pass
    return defaults

def _identity():
    defaults = {"device_name": "DenseAI Edge", "device_id": "RK3588-EDGE-001", "station_id": "未设置", "hostname": socket.gethostname()}
    try: defaults.update(json.loads(IDENTITY_FILE.read_text(encoding="utf-8")))
    except (OSError, ValueError): pass
    defaults.setdefault("hostname", socket.gethostname())
    return defaults

def _network():
    host = socket.gethostname()
    ip = "--"
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); probe.connect(("8.8.8.8", 80)); ip = probe.getsockname()[0]; probe.close()
    except OSError:
        try: ip = socket.gethostbyname(host)
        except OSError: pass
    mac = "--"
    for p in Path("/sys/class/net").glob("*/address"):
        if p.parent.name != "lo":
            try: mac = p.read_text().strip().upper(); break
            except OSError: pass
    cfg = _json(NETWORK_FILE, {"interface":"自动选择", "mode":"dhcp", "static_ip":"", "netmask":"255.255.255.0", "gateway":"", "dns":"8.8.8.8", "wifi_ssid":""})
    return {**cfg, "ip": ip, "hostname": host, "mac": mac, "status": "已连接" if ip not in ("--", "127.0.0.1") else "未连接", "link_speed":"未知"}

def overview():
    usage = shutil.disk_usage(ROOT)
    return {"identity": _identity(), "network": _network(), "time": time.strftime("%Y-%m-%d %H:%M:%S %Z"), "time_config": _json(TIME_FILE, {"timezone":"UTC", "ntp_enabled":True, "ntp_server":"pool.ntp.org"}), "storage": {"total_gb": round(usage.total/1e9, 2), "used_gb": round((usage.total-usage.free)/1e9, 2), "free_gb": round(usage.free/1e9, 2), **_json(STORAGE_FILE, {"auto_cleanup_days":30,"video_retention_enabled":True,"low_space_threshold_percent":10})}, "services": {"backend": "运行中", "mediamtx": "由算法服务按需管理", **_json(SERVICES_FILE, {"api_port":8080,"node_red_port":1880})}, "peripherals": {"alarm_light": {"connected": Path("/dev/ch341-light").exists(), "path": "/dev/ch341-light", "baudrate": 9600}}}

def put_identity(payload):
    current = _identity(); current.update({k: str(payload[k]).strip() for k in ("device_name", "device_id", "station_id", "hostname") if k in payload and str(payload[k]).strip()})
    IDENTITY_FILE.parent.mkdir(parents=True, exist_ok=True); IDENTITY_FILE.write_text(json.dumps(current, ensure_ascii=False, indent=2), encoding="utf-8"); return current
def put_config(path, payload, defaults):
    value = _json(path, defaults); value.update(payload); path.parent.mkdir(parents=True, exist_ok=True); path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8"); return value

def backup():
    BACKUP_DIR.mkdir(parents=True, exist_ok=True); out = BACKUP_DIR / ("device-config-" + time.strftime("%Y%m%d-%H%M%S") + ".json")
    configs = {"identity": _identity(), "network": _json(NETWORK_FILE, {}), "time": _json(TIME_FILE, {}), "storage_policy": _json(STORAGE_FILE, {}), "services": _json(SERVICES_FILE, {})}
    out.write_text(json.dumps({"version": 1, "created_at": time.time(), "configs": configs}, ensure_ascii=False, indent=2), encoding="utf-8"); return {"file": str(out.relative_to(ROOT)), "created_at": time.time()}

def restore_latest():
    files = sorted(BACKUP_DIR.glob("device-config-*.json"), reverse=True)
    if not files: raise RuntimeError("没有可恢复的配置备份")
    data = json.loads(files[0].read_text(encoding="utf-8")); configs = data.get("configs", data)
    if "identity" in configs: put_identity(configs["identity"])
    for key, path in (("network", NETWORK_FILE), ("time", TIME_FILE), ("storage_policy", STORAGE_FILE), ("services", SERVICES_FILE)):
        if key in configs: put_config(path, configs[key], {})
    return {"file": str(files[0].relative_to(ROOT)), "message": "已恢复最近配置备份"}

def handle_get(path):
    if path == "/api/network": return 200, _network()
    if path == "/api/device/overview": return 200, overview()
    if path == "/api/device/health": return 200, overview()
    if path == "/api/peripherals/alarm-light": return 200, overview()["peripherals"]["alarm_light"]
    return None

def handle_post(path, payload):
    if path == "/api/config/backup": return 200, backup()
    if path == "/api/network/test":
        started = time.monotonic()
        try:
            with urllib.request.urlopen("https://example.com", timeout=3) as response: online = 200 <= response.status < 500
        except Exception: online = False
        return 200, {"ok": online, "latency_ms": round((time.monotonic()-started)*1000, 1), "message": "网络连通性正常" if online else "无法访问外部网络", "network": _network()}
    if path == "/api/peripherals/alarm-light/test":
        port = Path("/dev/ch341-light")
        if not port.exists(): return 200, {"ok": False, "message": "报警器未连接"}
        try:
            fd = os.open(str(port), os.O_RDWR | os.O_NOCTTY | os.O_SYNC)
            try:
                attrs = termios.tcgetattr(fd); attrs[0] = 0; attrs[1] = 0; attrs[2] = termios.CLOCAL | termios.CREAD | termios.CS8; attrs[3] = 0; attrs[4] = termios.B9600; attrs[5] = termios.B9600; attrs[6][termios.VMIN] = 0; attrs[6][termios.VTIME] = 10; termios.tcsetattr(fd, termios.TCSANOW, attrs)
                # Same protocol as SerialLightController::TurnRedFlashBeep:
                # FF 04 02 04 AA (red light + flashing + buzzer).
                os.write(fd, bytes((0xFF, 0x04, 0x02, 0x04, 0xAA))); termios.tcdrain(fd)
            finally: os.close(fd)
            return 200, {"ok": True, "message": "报警器测试指令已发送：FF 04 02 04 AA", "command_hex": "FF 04 02 04 AA"}
        except OSError as exc: return 200, {"ok": False, "message": "报警器通信失败：" + str(exc)}
    if path == "/api/config/restore": return 200, restore_latest()
    if path == "/api/services/restart": return 200, {"ok": True, "message": "服务重启请求已登记；请通过系统服务管理器执行重启"}
    if path == "/api/system/clear-logs":
        removed = 0
        for p in (ROOT / "runtime_logs").glob("*.log"):
            try: p.write_text("", encoding="utf-8"); removed += 1
            except OSError: pass
        return 200, {"ok": True, "removed": removed, "message": f"已清理 {removed} 个日志文件"}
    return None

def handle_put(path, payload):
    if path == "/api/network": return 200, {"network": put_config(NETWORK_FILE, payload, {"interface":"自动选择","mode":"dhcp","static_ip":"","netmask":"255.255.255.0","gateway":"","dns":"8.8.8.8","wifi_ssid":""})}
    if path == "/api/time": return 200, {"time": put_config(TIME_FILE, payload, {"timezone":"UTC","ntp_enabled":True,"ntp_server":"pool.ntp.org"})}
    if path == "/api/storage-policy": return 200, {"storage": put_config(STORAGE_FILE, payload, {"auto_cleanup_days":30,"video_retention_enabled":True,"low_space_threshold_percent":10})}
    if path == "/api/services/config": return 200, {"services": put_config(SERVICES_FILE, payload, {"api_port":8080,"node_red_port":1880})}
    return None
