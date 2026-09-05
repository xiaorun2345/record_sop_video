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
  resolutions?: string[];
  input_type?: "orbbec" | "video";
  input_uri?: string;
  local_videos?: string[];
}

export function getCameraStatus() {
  return requestJson<CameraStatus>("/camera/status");
}

export interface RecordingItem {
  filename: string;
  size_bytes: number;
  size_mb: number;
  modified_at: number;
}

export function getRecordings() {
  return requestJson<{ items: RecordingItem[] }>("/recordings");
}

export function startCamera(resolution?: string) {
  return requestJson<CameraStatus>("/camera/start", { method: "POST", body: JSON.stringify(resolution ? { resolution } : {}) });
}

export function stopCamera() {
  return requestJson<CameraStatus>("/camera/stop", { method: "POST", body: JSON.stringify({}) });
}

export function setCameraResolution(resolution: string) {
  return requestJson<CameraStatus>("/camera/resolution", { method: "POST", body: JSON.stringify({ resolution }) });
}

export function selectVideoSource(type: "camera" | "video", uri = "") {
  return requestJson<CameraStatus>("/video-sources/select", {
    method: "POST",
    body: JSON.stringify({ type, uri }),
  });
}

export async function uploadLocalVideo(file: File) {
  return requestJson<{ ok: boolean; filename: string; uri: string; message: string }>(
    `/video-sources/upload?filename=${encodeURIComponent(file.name)}`,
    { method: "POST", body: file, signal: AbortSignal.timeout(300_000) },
  );
}
