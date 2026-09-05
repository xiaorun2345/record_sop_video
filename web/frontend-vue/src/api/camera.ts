import { requestJson } from "@/api/http";

export interface CameraStatus {
  camera: "running" | "stopped";
  mediaMtx: "running" | "stopped";
  rawStreamReady: boolean;
  streamReady: boolean;
  streamPath: string;
  webrtcPort: number;
  resolution: string;
  cameraPid: number | null;
}

export function getCameraStatus() {
  return requestJson<CameraStatus>("/camera/status");
}

export function startCamera(resolution = "640x480") {
  return requestJson<CameraStatus>("/camera/start", { method: "POST", body: JSON.stringify({ resolution }) });
}

export function stopCamera() {
  return requestJson<CameraStatus>("/camera/stop", { method: "POST", body: JSON.stringify({}) });
}
