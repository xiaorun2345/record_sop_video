# 算法结果视频推流

本项目不使用 MJPEG/JPEG 流。算法进程在 `ProcessFrame()` 完成检测框、手部关键点和状态信息绘制后，将最终 BGR 帧送入 GStreamer `mpph264enc`，通过 RTMP 推送给 MediaMTX。

## 启动

先启动 MediaMTX：

```bash
./scripts/run_mediamtx.sh
```

再启动算法推流：

```bash
./scripts/run_sop_stream.sh
```

默认输入为 `rtmp://127.0.0.1:1935/sop`。浏览器使用 MediaMTX WebRTC 页面：

```text
http://设备IP:8889/sop
```

工作台也支持嵌入该 WebRTC 页面：

```text
http://设备IP:4173/web/frontend/index.html?mediamtx=http://设备IP:8889
```

MediaMTX 同时提供 HLS：`http://设备IP:8888/sop/index.m3u8`，以及 API：`http://设备IP:9997/v3/paths/list`。
