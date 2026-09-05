# DenseAI Edge：RK3588 RGB-D SOP 视觉平台

DenseAI Edge 是一套运行在 Rockchip RK3588 工业边缘设备上的视觉 SOP 平台。系统把 RGB-D 摄像头、YOLOv8 目标检测、手部检测与 21 点关键点、三维骨骼约束、SOP 规则判断、Web 工作台和 WebRTC 推流整合在同一条运行链路中。

本文档对应今日更新版本：`backup/device-system-fastapi-20260905`（2026-09-05）。

## 能力概览

- 支持本地视频、普通摄像头、GStreamer 和 Orbbec Gemini RGB-D 相机。
- 使用 RKNN 调用 YOLOv8 目标检测模型和手部 palm/landmark 模型。
- 对目标框、手腕、手心和 21 个手部关键点查询米制三维坐标。
- 提供固定骨长、异常点拒绝、时序平滑和短时遮挡预测。
- SOP 支持有序执行和无序执行：打螺丝等流程使用有序模式，包装清单等流程可使用无序模式。
- 必检对象支持数量、目标 ROI、手部 ROI、对象重叠关系和手物三维距离。
- 支持连续确认帧、最短驻留时间、步骤超时和告警信息。
- Web 工作台实时显示当前步骤、每项条件、当前数量、历史最佳数量、确认帧和告警。
- Web 工作台可独立控制目标框、手部框、骨骼约束、21 点和调试面板的画面叠加。
- MediaMTX 提供 raw 和 sop 两路 RTMP/WebRTC 流。
- FastAPI 提供设备、系统、模型、SOP 和运行时控制接口。

## 系统架构

```text
Orbbec RGB-D / 视频输入
          │
          ▼
    VideoSource 读取帧
          │
          ├── YOLOv8 RKNN 目标检测
          ├── Palm + Landmark RKNN 手部检测
          ├── RGB-D 三维查询
          └── 手部骨骼约束
          │
          ▼
    SopStateMachine 规则判断
          │
          ├── 结果叠加到视频帧
          ├── 推送 MediaMTX sop 流
          └── 写入运行快照 JSON
          │
          ▼
    FastAPI / Vue 3 Web 工作台
```

系统只运行一个算法进程和一个 FastAPI 服务。SOP 配置页面不会启动第二套摄像头或第二个后端。

## 目录结构

```text
.
├── CMakeLists.txt
├── config/
│   ├── sop_config.txt             # C++ 运行配置和发布后的 SOP 条件
│   ├── demo.mp4                   # 本地视频示例
│   └── orbbec_intrinsics.yml      # 标定参数
├── include/                       # 公共数据结构和模块接口
├── src/
│   ├── main.cpp                   # 采集、推理、状态机、推流主循环
│   ├── video_source.cpp           # 视频/Orbbec 输入和三维查询
│   ├── yolov8_detector.cpp        # YOLOv8 RKNN 推理和后处理
│   ├── hand_pose_detector.cpp     # palm + landmark RKNN 推理
│   ├── hand_skeleton_constraint.cpp
│   ├── sop_state_machine.cpp      # 有序/无序条件判断
│   ├── sop_runtime_snapshot.cpp   # 原子写运行快照
│   ├── visualization_flags.cpp    # 读取 Web 画面开关
│   ├── visualizer.cpp             # 检测框、手部骨架和关键点绘制
│   └── stream_publisher.cpp       # GStreamer/RTMP 推流
├── web/backend/                   # FastAPI 和运行控制
├── web/frontend-vue/              # Vue 3 单页应用
├── scripts/                       # 构建、后端、MediaMTX 脚本
└── third_party/                   # RKNN、Orbbec、YOLO 导出和算法依赖
```

## 运行前提

板端需要准备：CMake、C++17 编译器、OpenCV、Rockchip RKNN runtime、Orbbec SDK（使用 RGB-D 相机时）、GStreamer、`mpph264enc` 和模型文件。

推荐模型：

```text
models/ai_sop_best_int8.rknn
models/hand_detector.rknn
models/hand_landmarks.rknn
```

## 构建

```bash
./scripts/build.sh
```

常用选项：

```bash
./scripts/build.sh --debug
./scripts/build.sh --orbbec
./scripts/build.sh --clean
```

正式程序输出到 `output/rk3588_sop`，运行库复制到 `output/lib/`。本地无硬件编译检查：

```bash
cmake -S . -B /tmp/rk3588_sop_build -DENABLE_RKNN=OFF -DENABLE_ORBBEC=OFF
cmake --build /tmp/rk3588_sop_build -j2
```

## 启动平台

分别启动 FastAPI 和 MediaMTX：

```bash
./scripts/run_backend.sh
./scripts/run_mediamtx.sh
```

生产模式默认关闭 Uvicorn 访问日志，降低高频工作台轮询产生的日志压力。如需调试请求日志：

```bash
SOP_ACCESS_LOG=1 ./scripts/run_backend.sh
```

进入工作台后点击“启动摄像头”和“启动算法”。后端负责启动算法、等待 raw/sop 流就绪并管理进程生命周期。

页面地址：

```text
工作台：       http://设备IP:8080/
SOP 配置：     http://设备IP:8080/sop-config
设备管理：     http://设备IP:8080/device
模型管理：     http://设备IP:8080/models
系统设置：     http://设备IP:8080/settings
OpenAPI 文档： http://设备IP:8080/api/docs
```

## SOP 配置和发布

1. 打开 `/sop-config`；
2. 新建或选择 SOP；
3. 选择有序或无序执行方式；
4. 添加步骤并绘制 ROI；
5. 配置手部 ROI、必检对象、数量、对象 ROI 和对象关系；
6. 设置确认帧、超时、最短驻留和手物距离；
7. 点击“保存并发布”。

发布后后端会保存 SQLite 记录，生成 `config/active_sop_judgement.json`，并将富配置转换到 C++ 使用的 `config/sop_config.txt`。算法主循环检测配置文件变化后会热加载并重置 SOP 状态。草稿不会参与算法判断；没有必检对象的步骤不能发布。

## 有序和无序模式

### 有序模式

适合打螺丝、装配、拆装等严格工艺。只有当前步骤满足条件并完成连续确认后，才会进入下一步。停用步骤会自动跳过。

### 无序模式

适合包装清单、物料齐套等场景。每个启用步骤独立累计确认帧，任意顺序完成；全部启用步骤完成后 SOP 结束。

## 判定条件

- 必检对象：满足 ROI 的目标数量必须达到配置数量；运行快照同时提供当前数量和历史最佳数量。
- 对象 ROI：使用检测框中心点判断是否落入 ROI 多边形，绑定多个 ROI 时命中任意一个即可。
- 手部 ROI：使用手腕关键点判断是否进入区域。
- 对象关系：目前支持 `overlaps`，两个目标框 IoU 不低于 `0.10` 时成立。
- 手物距离：手腕与目标框中心的三维欧氏距离必须不超过配置阈值，设置为 0 表示关闭。
- 稳定确认：所有条件满足时 `confirmCount + 1`，任意条件不满足时归零，达到 `minConfirmFrames` 后通过。

## 工作台实时状态

工作台每 500ms 读取：

```text
GET /api/algorithm/status
GET /api/sop/runtime
GET /api/sop/runtime/state
```

C++ 将最近一帧判定结果原子写入 `/tmp/rk3588_sop_runtime_state.json`。快照包含当前步骤、步骤状态、对象数量、ROI/关系结果、确认帧、告警和帧号。文件超过约 2 秒未更新时，接口返回 `state=stale`。

常用控制：

```text
POST /api/camera/start
POST /api/camera/stop
POST /api/algorithm/start
POST /api/algorithm/stop
POST /api/sop/runtime/reset
```

## 画面叠加控制

工作台支持独立控制：目标检测框、手部检测框、手部骨骼约束、21 个关键点和算法调试面板。顶部手部总开关只是快捷全开/全关，子开关可以单独使用。

```text
GET /api/visualization/settings
PUT /api/visualization/settings
```

例如只关闭骨骼：

```bash
curl -X PUT http://设备IP:8080/api/visualization/settings \
  -H 'Content-Type: application/json' \
  -d '{"skeleton":false}'
```

C++ 通过 `/tmp/rk3588_sop_visual_<name>.off` 读取关闭标记，下一帧生效，无需重启算法。

## 视频流

MediaMTX 默认端口：RTMP `1935`、WebRTC `8889`、API `9997`。流路径为 `raw`（原始画面）和 `sop`（算法叠加画面）。浏览器根据当前页面 hostname 连接 WebRTC WHEP，不应把 `127.0.0.1` 地址直接提供给远程浏览器。

## 稳定性与压力测试

当前版本已验证：

- 实时状态接口并发 1000 次全部 HTTP 200；
- 画面开关并发更新 300 次全部 HTTP 200；
- 持续运行约 40 秒，FPS 约 29.4–30.4；
- 算法 RSS 约 140MB 且无持续增长；
- 快照延迟低于 0.11 秒；
- 日志限频后无持续刷屏；
- raw/sop 流可以正常恢复。

建议现场检查：

```bash
curl http://设备IP:8080/api/algorithm/status
curl http://设备IP:8080/api/sop/runtime/state
pgrep -af 'uvicorn|mediamtx|rk3588_sop'
tail -f runtime_logs/backend.log
tail -f runtime_logs/algorithm.log
```

重点观察 FPS、RSS、快照更新时间、MediaMTX 流状态、连续算法失败和 Orbbec 设备断开情况。

## 故障排查

### 页面打不开

```bash
hostname -I
ss -ltnp | grep ':8080'
curl http://设备IP:8080/
```

确认使用的是设备当前 IP；如果页面显示旧版本，浏览器执行 `Ctrl+F5`。

### 页面没有视频

检查 `/api/algorithm/status` 中的 `camera`、`raw_stream_ready`、`stream_ready`，确认 MediaMTX 监听 `8889`，并确认浏览器所在电脑可以访问设备的 TCP/UDP WebRTC 端口。

### 算法启动失败

```bash
tail -100 runtime_logs/algorithm.log
```

重点检查 RKNN 模型、运行库、Orbbec USB 权限、配置格式和 MediaMTX RTMP 端口。

### SOP 状态不更新

```bash
stat /tmp/rk3588_sop_runtime_state.json
cat /tmp/rk3588_sop_runtime_state.json
```

文件不存在或超过 2 秒未更新时，检查算法进程、算法模式标记和算法日志。

### Orbbec 不支持帧同步

部分设备不支持硬件 frame sync。系统会记录提示并继续使用 D2C 对齐，这不是致命错误；只有同时出现空帧、设备断开或流停止时才需要检查 USB3、供电和 SDK 版本。

## 开发验证

```bash
git diff --check
python3 -m py_compile web/backend/*.py
cmake --build /tmp/rk3588_sop_build -j2
npm --prefix web/frontend-vue run build
```

手部骨骼测试：

```bash
cmake -S . -B /tmp/rk3588_sop_tests \
  -DBUILD_HAND_SKELETON_TESTS=ON \
  -DENABLE_RKNN=OFF \
  -DENABLE_ORBBEC=OFF
cmake --build /tmp/rk3588_sop_tests -j2
ctest --test-dir /tmp/rk3588_sop_tests --output-on-failure
```

## Git 分支和发布

当前发布线：

```text
backup/device-system-fastapi-20260905
```

历史分支：

- `rgbd-baseline-2026-08-21`：RGB-D 感知基线；
- `ai-sop-expanded-20260829`：AI SOP 扩展和模型导出资源；
- `backup/device-system-fastapi-20260904`：设备管理、系统设置和 FastAPI 阶段性备份。

发布前应完成构建检查、`git diff --check`、板端相机/流/SOP 验证，并排除 `runtime_logs/`、录像、运行库、缓存和设备本地配置。

## 第三方组件

工程使用 Rockchip RKNN runtime、Orbbec SDK、OpenCV、GStreamer、MediaMTX、Ultralytics YOLOv8 适配代码和 Hungarian algorithm。部署和再分发时应分别遵守各组件许可证及硬件厂商条款。
