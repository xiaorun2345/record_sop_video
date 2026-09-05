import { computed, nextTick, onBeforeUnmount, onMounted, ref } from "vue";
import { getCameraStatus, startCamera as requestCameraStart, stopCamera as requestCameraStop } from "@/api/camera";
import type { CameraStatus } from "@/api/camera";

const STATUS_INTERVAL_MS = 2_000;
const STREAM_WAIT_TIMEOUT_MS = 30_000;

export function useCameraStream() {
  const selectedResolution = ref("640x480");
  const videoElement = ref<HTMLVideoElement | null>(null);
  const cameraStatus = ref<CameraStatus | null>(null);
  const cameraBusy = ref(false);
  const streamConnecting = ref(false);
  const streamReady = ref(false);
  const streamError = ref("");
  const videoWidth = ref(0);
  const videoHeight = ref(0);
  let peerConnection: RTCPeerConnection | null = null;
  let statusTimer: number | undefined;

  const cameraRunning = computed(() => cameraStatus.value?.camera === "running");
  const videoReady = computed(() => cameraRunning.value && streamReady.value && videoWidth.value > 0 && videoHeight.value > 0);
  const cameraStateLabel = computed(() => {
    if (cameraBusy.value && !cameraRunning.value) return "正在打开";
    if (cameraBusy.value && cameraRunning.value) return "正在关闭";
    if (streamConnecting.value) return "正在连接视频流";
    if (cameraRunning.value && streamReady.value) return "运行中";
    if (cameraRunning.value) return "摄像头已打开，等待视频流";
    return "已关闭";
  });

  async function startCamera(resolution = selectedResolution.value): Promise<string | null> {
    if (cameraBusy.value || cameraRunning.value) return null;
    cameraBusy.value = true;
    streamError.value = "";
    try {
      selectedResolution.value = resolution;
      applyStatus(await requestCameraStart(resolution));
      await nextTick();
      await waitForRawStream();
      return null;
    } catch (error) {
      const message = errorMessage(error, "摄像头启动失败");
      streamError.value = message;
      return message;
    } finally {
      cameraBusy.value = false;
    }
  }

  async function stopCamera(): Promise<string | null> {
    if (cameraBusy.value || !cameraRunning.value) return null;
    cameraBusy.value = true;
    streamError.value = "";
    closeStream();
    try {
      applyStatus(await requestCameraStop());
      return null;
    } catch (error) {
      const message = errorMessage(error, "摄像头关闭失败");
      streamError.value = message;
      await refreshStatus(false);
      return message;
    } finally {
      cameraBusy.value = false;
    }
  }

  async function refreshStatus(connectWhenReady = true): Promise<void> {
    try {
      const status = await getCameraStatus();
      applyStatus(status);
      if (!cameraRunning.value) {
        closeStream();
        return;
      }
      if (connectWhenReady && status.rawStreamReady && !peerConnection && !streamConnecting.value) {
        await nextTick();
        await connectStream(status);
      }
    } catch (error) {
      if (!cameraStatus.value) streamError.value = errorMessage(error, "无法读取摄像头状态");
    }
  }

  function updateVideoMetadata() {
    videoWidth.value = videoElement.value?.videoWidth ?? 0;
    videoHeight.value = videoElement.value?.videoHeight ?? 0;
  }

  async function waitForRawStream(): Promise<void> {
    const deadline = Date.now() + STREAM_WAIT_TIMEOUT_MS;
    while (Date.now() < deadline) {
      const status = await getCameraStatus();
      applyStatus(status);
      if (!cameraRunning.value) throw new Error("摄像头进程已退出");
      if (status.rawStreamReady) {
        await nextTick();
        await connectStream(status);
        return;
      }
      await delay(500);
    }
    throw new Error("摄像头已打开，但实时视频流在 30 秒内未就绪");
  }

  async function connectStream(status: CameraStatus): Promise<void> {
    if (peerConnection || streamConnecting.value) return;
    const video = videoElement.value;
    if (!video) throw new Error("视频画面尚未初始化");
    streamConnecting.value = true;
    streamReady.value = false;
    const connection = new RTCPeerConnection();
    peerConnection = connection;
    connection.ontrack = (event) => {
      const stream = event.streams[0];
      if (!stream || video.srcObject === stream) return;
      video.srcObject = stream;
      void video.play().catch(() => undefined);
      streamReady.value = true;
      streamConnecting.value = false;
      streamError.value = "";
    };
    connection.onconnectionstatechange = () => {
      if (["failed", "closed", "disconnected"].includes(connection.connectionState)) {
        streamReady.value = false;
        if (connection.connectionState === "failed") streamError.value = "实时视频流连接失败";
      }
    };
    connection.addTransceiver("video", { direction: "recvonly" });
    try {
      const offer = await connection.createOffer();
      await connection.setLocalDescription(offer);
      await waitForIceGathering(connection);
      const protocol = window.location.protocol === "https:" ? "https:" : "http:";
      const endpoint = `${protocol}//${window.location.hostname}:${status.webrtcPort}/${status.streamPath}/whep`;
      const response = await fetch(endpoint, {
        method: "POST",
        headers: { "Content-Type": "application/sdp" },
        body: connection.localDescription?.sdp ?? "",
      });
      if (!response.ok) throw new Error(`WebRTC WHEP 请求失败（HTTP ${response.status}）`);
      await connection.setRemoteDescription({ type: "answer", sdp: await response.text() });
    } catch (error) {
      closeStream();
      throw error;
    } finally {
      streamConnecting.value = false;
    }
  }

  function closeStream() {
    const video = videoElement.value;
    if (video) {
      video.pause();
      video.srcObject = null;
    }
    peerConnection?.close();
    peerConnection = null;
    streamConnecting.value = false;
    streamReady.value = false;
    videoWidth.value = 0;
    videoHeight.value = 0;
  }

  function applyStatus(status: CameraStatus) {
    cameraStatus.value = status;
    if (status.resolution) selectedResolution.value = status.resolution;
  }

  onMounted(() => {
    void refreshStatus();
    statusTimer = window.setInterval(() => void refreshStatus(), STATUS_INTERVAL_MS);
  });

  onBeforeUnmount(() => {
    if (statusTimer !== undefined) window.clearInterval(statusTimer);
    closeStream();
  });

  return {
    videoElement,
    cameraStatus,
    cameraBusy,
    cameraRunning,
    cameraStateLabel,
    streamConnecting,
    streamReady,
    streamError,
    videoReady,
    videoWidth,
    videoHeight,
    selectedResolution,
    startCamera,
    stopCamera,
    updateVideoMetadata,
  };
}

function waitForIceGathering(connection: RTCPeerConnection): Promise<void> {
  if (connection.iceGatheringState === "complete") return Promise.resolve();
  return new Promise((resolve) => {
    const finish = () => {
      window.clearTimeout(timeout);
      connection.removeEventListener("icegatheringstatechange", handleChange);
      resolve();
    };
    const handleChange = () => {
      if (connection.iceGatheringState !== "complete") return;
      finish();
    };
    const timeout = window.setTimeout(finish, 3_000);
    connection.addEventListener("icegatheringstatechange", handleChange);
  });
}

function delay(milliseconds: number) {
  return new Promise((resolve) => window.setTimeout(resolve, milliseconds));
}

function errorMessage(error: unknown, fallback: string) {
  return error instanceof Error && error.message ? error.message : fallback;
}
