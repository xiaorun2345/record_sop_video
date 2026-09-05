#!/usr/bin/env python3
"""B/S backend: controls the algorithm and MediaMTX processes."""
import json, os, re, signal, socket, subprocess, threading, time, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
try:
    from . import device_api, system_api
except ImportError:
    import device_api, system_api

BACKEND_DIR = Path(__file__).resolve().parent
WEB = BACKEND_DIR.parent
ROOT = WEB.parent
HOST = os.environ.get("SOP_API_HOST", "0.0.0.0")
PORT = int(os.environ.get("SOP_API_PORT", "8080"))
MEDIAMTX = WEB / "mediamtx/mediamtx"
MEDIAMTX_CFG = WEB / "mediamtx/mediamtx.yml"
ALGORITHM = ROOT / "output/rk3588_sop"
CONFIG = ROOT / "config/sop_config.txt"
LOG_DIR = ROOT / "runtime_logs"; LOG_DIR.mkdir(exist_ok=True)
MODE_FILE = Path("/tmp/rk3588_sop_algorithm_enabled")
RECORDING_CONTROL = Path("/tmp/rk3588_sop_recording")
HAND_LANDMARKS_VISIBILITY = Path("/tmp/rk3588_sop_hand_landmarks_visible")
RECORDING_DIR = ROOT / "output" / "recordings"
LOCAL_VIDEO_DIR = ROOT / "output" / "input_videos"
NPU_LATENCY_FILE = Path("/tmp/rk3588_sop_npu_latency_ms")
RUNTIME_STATE_FILE = Path("/tmp/rk3588_sop_runtime_state.json")
SOP_RESET_FILE = Path("/tmp/rk3588_sop_reset")
VIDEO_FINISHED_FILE = Path("/tmp/rk3588_sop_video_finished")
VISUALIZATION_DEFAULTS = {"hand_landmarks": True, "object_boxes": True, "hand_box": False, "skeleton": True, "keypoints": True, "debug_panel": True}
lock = threading.RLock(); mediamtx_proc = None; algorithm_proc = None
_media_paths_cache = {"sop": False, "raw": False}
_media_paths_cache_at = 0.0
_recordings_cache = []
_recordings_cache_at = 0.0
# A fresh web session always starts with algorithm processing disabled.  The
# marker is runtime state, not persistent configuration; stale markers made a
# newly opened page appear to have started the algorithm by itself.
MODE_FILE.unlink(missing_ok=True)
RECORDING_CONTROL.unlink(missing_ok=True)
RUNTIME_STATE_FILE.unlink(missing_ok=True)
SOP_RESET_FILE.unlink(missing_ok=True)
VIDEO_FINISHED_FILE.unlink(missing_ok=True)
HAND_LANDMARKS_VISIBILITY.touch()

class _ExistingProcess:
    """Small Popen-compatible handle for a process adopted after a backend restart."""
    def __init__(self, pid): self.pid = pid
    def poll(self):
        try:
            os.kill(self.pid, 0)
            return None
        except OSError:
            return 0
    def wait(self, timeout=None):
        deadline = time.time() + (timeout or 5)
        while self.poll() is None and time.time() < deadline: time.sleep(0.05)
        return self.poll() or 0

def alive(proc): return proc is not None and proc.poll() is None

def _find_process(executable):
    """Find an already running process for this exact executable path."""
    expected = executable.resolve()
    for entry in Path("/proc").glob("[0-9]*"):
        try:
            if (entry / "exe").resolve() == expected:
                return _ExistingProcess(int(entry.name))
        except (OSError, ValueError):
            continue
    return None

def _adopt_existing_locked():
    global mediamtx_proc, algorithm_proc
    if not alive(mediamtx_proc):
        mediamtx_proc = _find_process(MEDIAMTX)
    if not alive(algorithm_proc):
        algorithm_proc = _find_process(ALGORITHM)
def read_number(path):
    try: return float(path.read_text(encoding="utf-8").strip())
    except (OSError, ValueError): return 0.0

def visualization_status():
    result = {name: default and not Path(f"/tmp/rk3588_sop_visual_{name}.off").exists()
              for name, default in VISUALIZATION_DEFAULTS.items()}
    result["hand_landmarks"] = result["hand_box"] and result["skeleton"] and result["keypoints"]
    return result
def cpu_temperature_c():
    candidates = []
    for zone in sorted(Path("/sys/class/thermal").glob("thermal_zone*")):
        try:
            value = read_number(zone / "temp")
            kind = (zone / "type").read_text(encoding="utf-8", errors="ignore").lower()
            if value > 0: candidates.append((0 if "cpu" in kind or "soc" in kind else 1, value / 1000.0 if value > 200 else value))
        except OSError: pass
    if not candidates: return 0.0
    candidates.sort(key=lambda item: (item[0], -item[1]))
    return candidates[0][1]
def process_error_message(proc):
    """Return a concise, actionable message when a child exits during startup."""
    code = proc.poll() if proc is not None else None
    try:
        log_path = LOG_DIR / "algorithm.log"
        lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        detail = next((line.strip() for line in reversed(lines) if line.strip()), "")
    except OSError:
        detail = ""
    return f"算法进程启动失败 (exit={code})" + (f": {detail}" if detail else "，请查看 runtime_logs/algorithm.log")
def port_open(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.15): return True
    except OSError: return False
def start_locked():
    global mediamtx_proc, algorithm_proc
    _adopt_existing_locked()
    if not MEDIAMTX.exists() or not ALGORITHM.exists(): raise RuntimeError("运行文件不存在，请先完成构建/部署")
    if not alive(mediamtx_proc) and not port_open(1935):
        log = open(LOG_DIR / "mediamtx.log", "ab"); mediamtx_proc = subprocess.Popen([str(MEDIAMTX), str(MEDIAMTX_CFG)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        # Do not race the RTMP publisher against MediaMTX startup.  Starting
        # the encoder before port 1935 is listening leaves appsrc in an error
        # state and the browser then shows a frozen/empty stream.
        for _ in range(30):
            if port_open(1935):
                break
            if not alive(mediamtx_proc):
                raise RuntimeError("MediaMTX 启动失败，请查看 runtime_logs/mediamtx.log")
            time.sleep(0.1)
        if not port_open(1935):
            raise RuntimeError("MediaMTX RTMP 端口 1935 未就绪，请查看 runtime_logs/mediamtx.log")
    if not alive(algorithm_proc):
        log = open(LOG_DIR / "algorithm.log", "ab"); env = os.environ.copy(); env.setdefault("RK3588_SOP_RTMP_URL", "rtmp://127.0.0.1:1935/sop"); env.setdefault("GST_MPP_NO_RGA", "1")
        env.setdefault("RK3588_SOP_NO_WINDOW", "1")
        # Keep runtime lookup deterministic when the service is launched from
        # systemd, a web server, or another working directory.
        runtime_lib = str(ROOT / "output/lib")
        env["LD_LIBRARY_PATH"] = runtime_lib + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
        VIDEO_FINISHED_FILE.unlink(missing_ok=True)
        algorithm_proc = subprocess.Popen([str(ALGORITHM), str(CONFIG)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        # Popen only means fork succeeded.  Detect immediate RKNN/USB/config
        # failures here so the browser receives a real error instead of a
        # misleading 200/stopped status.
        for _ in range(20):
            if not alive(algorithm_proc):
                message = process_error_message(algorithm_proc)
                algorithm_proc = None
                raise RuntimeError(message)
            time.sleep(0.05)

def camera_start_locked():
    start_locked()
    MODE_FILE.unlink(missing_ok=True)
    if not _wait_for_stream("raw", 8.0):
        raise RuntimeError("摄像头进程已启动，但原始视频流未就绪，请检查 runtime_logs/algorithm.log")

def camera_stop_locked():
    MODE_FILE.unlink(missing_ok=True)
    RECORDING_CONTROL.unlink(missing_ok=True)
    stop_locked()

def algorithm_start_locked():
    global algorithm_proc
    # For a file source, camera_start_locked() is used as video preview and the
    # capture thread advances through the file even when algorithm processing is
    # disabled.  Restart the algorithm process before enabling inference so each
    # run reopens the selected recording and starts analysis from frame 1.
    if input_source_type() == "video" and alive(algorithm_proc):
        stop_proc(algorithm_proc)
        algorithm_proc = None
        SOP_RESET_FILE.unlink(missing_ok=True)
        RUNTIME_STATE_FILE.unlink(missing_ok=True)
    SOP_RESET_FILE.touch()
    MODE_FILE.touch()
    if not alive(algorithm_proc):
        start_locked()
    if not _wait_for_stream("sop", 8.0):
        MODE_FILE.unlink(missing_ok=True)
        raise RuntimeError("算法进程已启动，但算法视频流未就绪，请检查 runtime_logs/algorithm.log")

def algorithm_stop_locked():
    MODE_FILE.unlink(missing_ok=True)

def set_resolution(value):
    """Persist the selected Orbbec color resolution in the shared config."""
    match = re.fullmatch(r"(1920)x(1080)|(640)x(480)", value or "")
    if not match:
        raise ValueError("不支持的分辨率，仅支持 1920x1080 或 640x480")
    width, height = ("1920", "1080") if value.startswith("1920") else ("640", "480")
    text = CONFIG.read_text(encoding="utf-8")
    text = re.sub(r"(?m)^input\.width=.*$", f"input.width={width}", text)
    text = re.sub(r"(?m)^input\.height=.*$", f"input.height={height}", text)
    temporary = CONFIG.with_suffix(CONFIG.suffix + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    os.replace(temporary, CONFIG)

def input_source_type():
    try:
        text = CONFIG.read_text(encoding="utf-8")
        match = re.search(r"(?m)^input\.type=(.*)$", text)
        return match.group(1).strip().lower() if match else "orbbec"
    except OSError:
        return "orbbec"

def set_input_source(source_type, uri=""):
    """Persist the input source used by the next algorithm process."""
    source_type = str(source_type or "").strip().lower()
    if source_type not in ("orbbec", "camera", "video"):
        raise ValueError("输入源必须是摄像头或本地视频")
    if source_type == "camera":
        source_type = "orbbec"
        uri = ""
    if source_type == "video":
        path = Path(str(uri)).resolve()
        allowed_dirs = (LOCAL_VIDEO_DIR.resolve(), RECORDING_DIR.resolve())
        if not path.is_file() or not any(directory in path.parents for directory in allowed_dirs):
            raise ValueError("视频文件不存在或不在服务器录像目录中")
        uri = str(path)
    text = CONFIG.read_text(encoding="utf-8")
    text = re.sub(r"(?m)^input\.type=.*$", f"input.type={source_type}", text)
    text = re.sub(r"(?m)^input\.uri=.*$", f"input.uri={uri}", text)
    temporary = CONFIG.with_suffix(CONFIG.suffix + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    os.replace(temporary, CONFIG)
def stop_proc(proc):
    if alive(proc):
        try: os.killpg(proc.pid, signal.SIGTERM); proc.wait(timeout=5)
        except (ProcessLookupError, subprocess.TimeoutExpired):
            if alive(proc): os.killpg(proc.pid, signal.SIGKILL)
def stop_locked():
    global mediamtx_proc, algorithm_proc
    _adopt_existing_locked()
    stop_proc(algorithm_proc); stop_proc(mediamtx_proc); algorithm_proc = None; mediamtx_proc = None
def status_locked():
    global _media_paths_cache, _media_paths_cache_at, _recordings_cache, _recordings_cache_at
    _adopt_existing_locked()
    host = os.environ.get("SOP_PUBLIC_HOST", "127.0.0.1")
    stream_ready = False
    raw_ready = False
    now = time.monotonic()
    if now - _media_paths_cache_at >= 0.25:
        try:
            with urllib.request.urlopen("http://127.0.0.1:9997/v3/paths/list", timeout=0.3) as response:
                payload = json.load(response)
                _media_paths_cache = {
                    "sop": any(item.get("name") == "sop" and item.get("ready") for item in payload.get("items", [])),
                    "raw": any(item.get("name") == "raw" and item.get("ready") for item in payload.get("items", [])),
                }
        except (OSError, ValueError):
            _media_paths_cache = {"sop": False, "raw": False}
        _media_paths_cache_at = now
    stream_ready = _media_paths_cache["sop"]
    raw_ready = _media_paths_cache["raw"]
    camera_running = alive(algorithm_proc)
    algorithm_running = camera_running and MODE_FILE.exists()
    mode = ""
    try: mode = RECORDING_CONTROL.read_text(encoding="utf-8").strip()
    except OSError: pass
    fps = read_number(Path("/tmp/rk3588_sop_fps"))
    if now - _recordings_cache_at >= 1.0:
        _recordings_cache = sorted((str(p.relative_to(ROOT)) for p in list(RECORDING_DIR.glob("*.mp4")) + list(RECORDING_DIR.glob("*.avi"))), reverse=True) if RECORDING_DIR.exists() else []
        _recordings_cache_at = now
    files = _recordings_cache
    resolution = "640x480"
    input_type = "orbbec"
    input_uri = ""
    try:
        text = CONFIG.read_text(encoding="utf-8")
        input_type_match = re.search(r"(?m)^input\.type=(.*)$", text)
        input_uri_match = re.search(r"(?m)^input\.uri=(.*)$", text)
        if input_type_match: input_type = input_type_match.group(1).strip()
        if input_uri_match: input_uri = input_uri_match.group(1).strip()
        width = re.search(r"(?m)^input\.width=(\d+)$", text)
        height = re.search(r"(?m)^input\.height=(\d+)$", text)
        if width and height: resolution = f"{width.group(1)}x{height.group(1)}"
    except OSError:
        pass
    media_running = alive(mediamtx_proc) or port_open(1935)
    visuals = visualization_status()
    local_videos = []
    if LOCAL_VIDEO_DIR.exists():
        for path in sorted(LOCAL_VIDEO_DIR.iterdir(), key=lambda p: p.stat().st_mtime, reverse=True):
            if path.is_file() and path.suffix.lower() in (".mp4", ".avi", ".mkv", ".mov", ".webm"):
                local_videos.append(path.name)
    return {
        "camera": "running" if camera_running else "stopped",
        "algorithm": "running" if algorithm_running else "stopped",
        "video_finished": VIDEO_FINISHED_FILE.exists(),
        "mediamtx": "running" if media_running else "stopped",
        "mediaMtx": "running" if media_running else "stopped",
        "raw_stream": f"http://{host}:8889/raw/",
        "stream": f"http://{host}:8889/sop/",
        "raw_stream_ready": raw_ready,
        "stream_ready": stream_ready,
        "rawStreamReady": raw_ready,
        "streamReady": stream_ready,
        "streamPath": "raw",
        "webrtcPort": 8889,
        "resolution": resolution,
        "resolutions": ["1920x1080", "640x480"],
        "input_type": input_type,
        "input_uri": input_uri,
        "local_videos": local_videos[:50],
        "algorithm_pid": algorithm_proc.pid if camera_running else None,
        "cameraPid": algorithm_proc.pid if camera_running else None,
        "fps": fps,
        "npu_latency_ms": read_number(NPU_LATENCY_FILE),
        "cpu_temperature_c": cpu_temperature_c(),
        "recording": mode or "stopped",
        "recording_dir": str(RECORDING_DIR),
        "recordings": files[:20],
        "latest_recording": files[0] if files else None,
        "hand_landmarks_visible": visuals["hand_landmarks"],
        "visualization": visuals,
    }

def _wait_for_stream(path_name, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with urllib.request.urlopen("http://127.0.0.1:9997/v3/paths/list", timeout=0.4) as response:
                payload = json.load(response)
            if any(item.get("name") == path_name and item.get("ready") for item in payload.get("items", [])):
                return True
        except (OSError, ValueError):
            pass
        if not alive(algorithm_proc):
            return False
        time.sleep(0.2)
    return False
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def send_json(self, code, value):
        data = json.dumps(value, ensure_ascii=False).encode(); self.send_response(code); self.send_header("Content-Type", "application/json; charset=utf-8"); self.send_header("Access-Control-Allow-Origin", "*"); self.send_header("Access-Control-Allow-Methods", "GET,POST,OPTIONS"); self.send_header("Access-Control-Allow-Headers", "Content-Type"); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
    def do_OPTIONS(self): self.send_json(204, {})
    def do_PUT(self):
        try:
            length = int(self.headers.get("Content-Length", "0") or 0)
            payload = json.loads(self.rfile.read(length).decode("utf-8")) if length else {}
            if self.path == "/api/device/identity":
                self.send_json(200, {"identity": device_api.put_identity(payload)}); return
            extra = device_api.handle_put(self.path, payload)
            if extra is not None:
                self.send_json(*extra); return
            self.send_json(404, {"error": "not found"})
        except Exception as exc: self.send_json(500, {"error": str(exc)})
    def do_GET(self):
        extra = device_api.handle_get(self.path) or system_api.handle_get(self.path)
        if extra is not None:
            self.send_json(*extra)
        elif self.path in ("/api/algorithm/status", "/api/recording/status", "/api/camera/status"):
            with lock: self.send_json(200, status_locked())
        elif self.path in ("/", "/index.html", "/app.js", "/styles.css", "/device.html", "/device.js", "/device.css", "/settings.html", "/settings.js", "/settings.css"):
            name = "index.html" if self.path in ("/", "/index.html") else self.path.lstrip("/")
            path = WEB / "frontend" / name
            if not path.is_file(): self.send_json(404, {"error": "not found"}); return
            data = path.read_bytes(); content_type = "text/html; charset=utf-8" if name.endswith(".html") else ("application/javascript; charset=utf-8" if name.endswith(".js") else "text/css; charset=utf-8")
            self.send_response(200); self.send_header("Content-Type", content_type); self.send_header("Cache-Control", "no-store, no-cache, must-revalidate"); self.send_header("Pragma", "no-cache"); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
        else: self.send_json(404, {"error": "not found"})
    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", "0") or 0)
            payload = json.loads(self.rfile.read(length).decode("utf-8")) if length else {}
            extra = device_api.handle_post(self.path, payload) or system_api.handle_post(self.path, payload)
            if extra is not None:
                self.send_json(*extra); return
            if self.path == "/api/camera/resolution":
                with lock:
                    if alive(algorithm_proc): raise ValueError("摄像头运行中不能切换分辨率，请先关闭摄像头")
                    set_resolution(payload.get("resolution", "")); result = status_locked()
            elif self.path in ("/api/camera/start", "/api/algorithm/start"):
                if payload.get("resolution") and input_source_type() != "video":
                    set_resolution(payload["resolution"])
                with lock:
                    if self.path == "/api/camera/start": camera_start_locked()
                    else: algorithm_start_locked()
                    result = status_locked()
            elif self.path in ("/api/camera/stop", "/api/algorithm/stop"):
                with lock:
                    if self.path == "/api/camera/stop": camera_stop_locked()
                    else: algorithm_stop_locked()
                    result = status_locked()
            elif self.path == "/api/recording/start":
                length = int(self.headers.get("Content-Length", "0") or 0)
                payload = json.loads(self.rfile.read(length).decode("utf-8")) if length else {}
                mode = payload.get("mode", "processed")
                if mode not in ("processed", "raw"): raise ValueError("录像模式必须是 processed 或 raw")
                with lock:
                    start_locked(); RECORDING_DIR.mkdir(parents=True, exist_ok=True); RECORDING_CONTROL.write_text(mode, encoding="utf-8"); result = status_locked()
            elif self.path == "/api/recording/stop":
                with lock:
                    RECORDING_CONTROL.unlink(missing_ok=True); result = status_locked()
            elif self.path == "/api/visualization/hand-landmarks":
                length = int(self.headers.get("Content-Length", "0") or 0)
                payload = json.loads(self.rfile.read(length).decode("utf-8")) if length else {}
                enabled = payload.get("enabled")
                if not isinstance(enabled, bool): raise ValueError("enabled 必须是布尔值")
                with lock:
                    if enabled: HAND_LANDMARKS_VISIBILITY.touch()
                    else: HAND_LANDMARKS_VISIBILITY.unlink(missing_ok=True)
                    result = status_locked()
            else: self.send_json(404, {"error": "not found"}); return
            self.send_json(200, result)
        except Exception as exc: self.send_json(500, {"error": str(exc)})
if __name__ == "__main__":
    print(f"SOP API listening on {HOST}:{PORT}", flush=True); ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
