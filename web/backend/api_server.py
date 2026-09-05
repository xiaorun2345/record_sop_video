from fastapi import FastAPI, Request
from fastapi.responses import RedirectResponse, FileResponse
from fastapi.responses import JSONResponse
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from .device_routes import router as device_router
from .system_routes import router as system_router
from .runtime_routes import router as runtime_router
from .sop_routes import router as sop_router
from .model_routes import router as model_router
from pathlib import Path

app = FastAPI(title="DenseAI Edge API", version="1.0.0", docs_url="/api/docs", redoc_url="/api/redoc")
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])
app.include_router(device_router); app.include_router(system_router); app.include_router(runtime_router); app.include_router(sop_router); app.include_router(model_router)
@app.exception_handler(Exception)
async def api_error(_request: Request, exc: Exception):
    return JSONResponse(status_code=500, content={"error": str(exc) or "服务器内部错误"})
WEB = Path(__file__).resolve().parents[1] / "frontend-vue" / "dist"
@app.get("/device.html")
def legacy_device(): return RedirectResponse("/device")
@app.get("/settings.html")
def legacy_settings(): return RedirectResponse("/settings")
@app.get("/device")
def device_spa(): return FileResponse(WEB / "index.html")
@app.get("/settings")
def settings_spa(): return FileResponse(WEB / "index.html")
@app.get("/sop-config")
def sop_config_spa(): return FileResponse(WEB / "index.html")
@app.get("/models")
def models_spa(): return FileResponse(WEB / "index.html")
app.mount("/", StaticFiles(directory=str(WEB), html=True), name="frontend")
