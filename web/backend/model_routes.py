from fastapi import APIRouter, Request
from pydantic import BaseModel
from . import model_api

router = APIRouter(prefix="/api", tags=["models"])


class ModelConfig(BaseModel):
    values: dict[str, object]


class ActivateModel(BaseModel):
    path: str


@router.get("/models")
def models():
    return model_api.overview()


@router.put("/models/config")
def save_config(value: ModelConfig):
    return model_api.save_config(value.values)


@router.post("/models/detector/activate")
def activate(value: ActivateModel):
    return model_api.activate_detector_model(value.path)


@router.post("/models/detector/upload")
async def upload(request: Request, filename: str):
    return model_api.upload_detector_model(filename, await request.body())
