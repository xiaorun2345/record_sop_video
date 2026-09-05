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
RECORDING_DIR = ROOT / "output" / "recordings"
LOG_DIR = ROOT / "runtime_logs"
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

def _read_number(path):
    try:
        return float(path.read_text(encoding="utf-8").strip().split()[0])
    except (OSError, ValueError):
        return None

def _thermal_zones():
    zones = []
    for zone in sorted(Path("/sys/class/thermal").glob("thermal_zone*")):
        temp = _read_number(zone / "temp")
        if temp is None:
            continue
        if temp > 200:
            temp /= 1000.0
        try:
            kind = (zone / "type").read_text(encoding="utf-8", errors="ignore").strip()
        except OSError:
            kind = zone.name
        label = kind or zone.name
        zones.append({"name": zone.name, "type": label, "label": label.upper(), "temperature_c": round(temp, 1)})
    return zones

def _cpu_temperature(zones):
    if not zones:
        return None
    preferred = [z for z in zones if any(token in z["type"].lower() for token in ("cpu", "soc", "package"))]
    source = preferred or zones
    return max(z["temperature_c"] for z in source)

def _memory():
    values = {}
    try:
        for line in Path("/proc/meminfo").read_text(encoding="utf-8", errors="ignore").splitlines():
            key, value = line.split(":", 1)
            values[key] = int(value.strip().split()[0]) * 1024
    except (OSError, ValueError):
        return {}
    total = values.get("MemTotal", 0)
    available = values.get("MemAvailable", 0)
    used = max(total - available, 0)
    return {
        "total_gb": round(total / 1e9, 2),
        "used_gb": round(used / 1e9, 2),
        "free_gb": round(available / 1e9, 2),
        "used_percent": round((used / total) * 100, 1) if total else 0,
    }

def _cpu_load():
    try:
        load1, load5, load15 = os.getloadavg()
    except OSError:
        return {}
    cores = os.cpu_count() or 1
    return {
        "cores": cores,
        "load1": round(load1, 2),
        "load5": round(load5, 2),
        "load15": round(load15, 2),
        "load_percent": round(min(load1 / cores * 100, 100), 1),
    }

def _uptime_seconds():
    value = _read_number(Path("/proc/uptime"))
    return round(value) if value is not None else 0

def _storage_health():
    usage = shutil.disk_usage(ROOT)
    policy = _json(STORAGE_FILE, {"auto_cleanup_days":30,"video_retention_enabled":True,"low_space_threshold_percent":10})
    used_percent = round((usage.total - usage.free) / usage.total * 100, 1) if usage.total else 0
    free_percent = round(usage.free / usage.total * 100, 1) if usage.total else 0
    threshold = int(policy.get("low_space_threshold_percent") or 10)
    status = "critical" if free_percent <= max(8, threshold / 2) else ("warning" if free_percent <= max(15, threshold) else "ok")
    return {
        "total_gb": round(usage.total/1e9, 2),
        "used_gb": round((usage.total-usage.free)/1e9, 2),
        "free_gb": round(usage.free/1e9, 2),
        "used_percent": used_percent,
        "free_percent": free_percent,
        "low_space_threshold_percent": threshold,
        "status": status,
    }

def storage_overview():
    usage = shutil.disk_usage(ROOT)
    recordings = []
    if RECORDING_DIR.exists():
        for path in RECORDING_DIR.iterdir():
            if path.is_file() and path.suffix.lower() in (".mp4", ".avi", ".mkv"):
                try:
                    stat = path.stat()
                    recordings.append({"filename": path.name, "size_bytes": stat.st_size, "modified_at": stat.st_mtime})
                except OSError:
                    continue
    logs = []
    if LOG_DIR.exists():
        for path in LOG_DIR.glob("*.log"):
            try:
                logs.append({"filename": path.name, "size_bytes": path.stat().st_size})
            except OSError:
                continue
    policy = _json(STORAGE_FILE, {"auto_cleanup_days": 30, "low_space_threshold_percent": 10})
    return {
        "disk": {
            "total_gb": round(usage.total / 1e9, 2),
            "used_gb": round((usage.total - usage.free) / 1e9, 2),
            "free_gb": round(usage.free / 1e9, 2),
            "used_percent": round((usage.total - usage.free) / usage.total * 100, 1) if usage.total else 0,
            "free_percent": round(usage.free / usage.total * 100, 1) if usage.total else 0,
            "status": _storage_health()["status"],
        },
        "recordings": {
            "count": len(recordings),
            "size_bytes": sum(item["size_bytes"] for item in recordings),
            "size_gb": round(sum(item["size_bytes"] for item in recordings) / 1e9, 2),
        },
        "logs": {
            "count": len(logs),
            "size_bytes": sum(item["size_bytes"] for item in logs),
            "size_kb": round(sum(item["size_bytes"] for item in logs) / 1024, 1),
        },
        "policy": {
            "auto_cleanup_days": int(policy.get("auto_cleanup_days") or 30),
            "low_space_threshold_percent": int(policy.get("low_space_threshold_percent") or 10),
        },
    }

def recordings_before(days):
    cutoff = time.time() - max(1, int(days)) * 86400
    if not RECORDING_DIR.exists():
        return []
    items = []
    for path in RECORDING_DIR.iterdir():
        if path.is_file() and path.suffix.lower() in (".mp4", ".avi", ".mkv"):
            try:
                stat = path.stat()
                if stat.st_mtime < cutoff:
                    items.append({"filename": path.name, "size_bytes": stat.st_size, "modified_at": stat.st_mtime})
            except OSError:
                continue
    return sorted(items, key=lambda item: item["modified_at"])

def cleanup_recordings(days):
    removed = []
    for item in recordings_before(days):
        path = RECORDING_DIR / item["filename"]
        try:
            path.unlink()
            removed.append(item)
        except OSError:
            continue
    return removed

def health():
    zones = _thermal_zones()
    cpu_temp = _cpu_temperature(zones)
    overview_data = overview()
    return {
        "summary": {
            "status": "warning" if overview_data["storage"]["free_gb"] <= 2 else "ok",
            "updated_at": time.strftime("%Y-%m-%d %H:%M:%S %Z"),
            "uptime_seconds": _uptime_seconds(),
        },
        "cpu": {**_cpu_load(), "temperature_c": cpu_temp},
        "temperatures": zones,
        "memory": _memory(),
        "storage": _storage_health(),
        "services": overview_data["services"],
        "peripherals": overview_data["peripherals"],
        "network": overview_data["network"],
    }

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
    if path == "/api/device/health": return 200, health()
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
