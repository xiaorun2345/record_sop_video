# RK3588 SOP 检测示例

这个工程用于在 RK3588 上跑一条装配场景检测链路：视频或 Gemini RGB-D 相机输入，YOLOv8 目标检测，手部检测与 21 点关键点检测，最后把检测结果、深度信息、耗时数据画到画面上并输出日志。

当前可回退基线版本说明：[`docs/releases/2026-08-21-rgbd-baseline.md`](docs/releases/2026-08-21-rgbd-baseline.md)。该版本是加入手部 21 点三维骨骼约束之前的稳定保存点。

YOLOv8 单输出 INT8 模型在 RK3588 上检测不到目标的问题记录：[`docs/yolov8-int8-single-output-issue-20260827.md`](docs/yolov8-int8-single-output-issue-20260827.md)。

当前 `main.cpp` 默认清空了配置里的 `steps` 和 `rois`，实际运行模式是检测与可视化链路。SOP 状态机、ROI、超时和 3D 距离规则代码已经保留在工程内，恢复配置后即可接回步骤判断。

## 备份分支说明：backup/device-system-fastapi-20260904

本分支是 2026-09-04 的设备管理与系统设置阶段性备份，基于 `ai-sop-expanded-20260829` 创建。主要内容：

- 使用 FastAPI + Uvicorn 重构后端，保留工作台原有摄像头、算法、录制和关键点控制接口。
- 新增设备管理页面：设备身份、网络/DHCP/静态 IP/Wi-Fi 配置、健康状态、报警器测试、配置备份/恢复、日志清理和维护操作。
- 新增系统设置页面：时区/NTP、存储策略、服务端口和日志设置。
- 新增 OpenAPI 文档：启动服务后访问 `/api/docs`。
- 工作台会自动同步设备名称、设备编号和工位编号。

启动后端：

```bash
./scripts/run_backend.sh
```

页面地址：

- 工作台：`http://设备IP:8080/`
- 设备管理：`http://设备IP:8080/device.html`
- 系统设置：`http://设备IP:8080/settings.html`
- API 文档：`http://设备IP:8080/api/docs`

本分支排除了运行日志、录像、私钥、Python 缓存和板端编译产物；设备本地配置仍保存在 `config/*.json`，不会覆盖其他环境的配置。网络配置当前完成校验和持久化，实际网卡切换需根据目标设备使用的 NetworkManager 或 systemd-networkd 继续接入。

## 目录结构

```text
.
├── CMakeLists.txt
├── config/
│   ├── sop_config.txt          # 主配置
│   ├── demo.mp4                # 本地视频示例
│   └── orbbec_intrinsics.yml   # 标定文件预留
├── include/                    # 公共数据结构和模块接口
├── models/                     # RKNN/ONNX/PT 模型
├── scripts/build.sh            # 板端编译脚本
├── src/
│   ├── main.cpp                # 主流程编排
│   ├── video_source.cpp        # video/camera/gstreamer/orbbec 输入
│   ├── yolov8_detector.cpp     # YOLOv8 RKNN 推理和后处理
│   ├── hand_pose_detector.cpp  # 手掌检测和 21 点关键点
│   ├── sop_state_machine.cpp   # SOP 步骤状态机
│   ├── geometry_utils.cpp      # ROI、IoU、点位工具
│   ├── config_loader.cpp       # key=value 配置解析
│   └── visualizer.cpp          # 检测框、手部骨架、耗时面板绘制
└── third_party/
    ├── orbbec_sdk/             # Orbbec SDK，本仓库按本地部署方式放置
    └── ultralytics_yolov8_rknn/ # YOLOv8 训练和 RKNPU 导出代码
```

## 主流程

程序入口在 `src/main.cpp`，默认读取 `config/sop_config.txt`：

1. 读取配置，初始化 YOLOv8 RKNN、手部 palm detector RKNN、hand landmark RKNN。
2. 根据 `input.type` 打开输入源。支持 `video`、`camera`、`gstreamer`、`orbbec`。
3. 每帧读取 `RgbdFrame`。普通视频没有深度，Orbbec 输入会返回 D2C 对齐后的彩色图和深度图。
4. 同一帧依次执行 YOLOv8 和手部检测。两个 RKNN context 串行调用，避免同时抢占 RK3588 NPU 队列。
5. 对目标框中心、手心和 21 个手部关键点查询真实 3D 坐标。
6. 对 21 点执行固定骨长、异常点拒绝、短时遮挡恢复和时序平滑。
7. 把约束后的 3D 骨架投影回彩色图，再交给状态机和可视化模块。

窗口左上角显示当前模式，右上角显示每帧耗时，包括读帧、YOLO、手部检测、3D 查询、状态机、绘制和整帧处理耗时。

## 主要算法

### YOLOv8 目标检测

`Yolov8Detector` 只实现 RKNN 后端。

- 预处理：原图按比例缩放到 `detector.input_size`，使用 114 灰色填充 letterbox 区域，输出 RGB888。
- 输入适配：运行时查询 RKNN 输入 tensor 属性，兼容 NHWC/NCHW 和 UINT8/FLOAT32 输入。
- 推理：通过 `rknn_inputs_set`、`rknn_run`、`rknn_outputs_get` 执行。
- 后处理：兼容两类 YOLOv8 输出。
  - rknn_model_zoo 常见三分支输出，按 box/class/score 分支解码，并使用 DFL softmax 期望值还原边框距离。
  - Ultralytics 常见单输出，支持 `[cx, cy, w, h, class...]` 和 `[cx, cy, w, h, obj, class...]`。
- 坐标恢复：扣除 letterbox padding，再除以缩放系数映射回原图。
- NMS：按置信度降序保留候选框，只抑制同类别且 IoU 超过阈值的低分框。

### 手部关键点检测

`HandPoseDetector` 使用两段 RKNN 模型：

- palm detector 输入 192x192 RGB letterbox 图。
- 按 MediaPipe palm 结构生成 anchor，解码手掌框和 7 个手掌关键点。
- 按分数排序，对 palm 框做 IoU 抑制，最多保留 `hand.max_num_hands` 只手。
- 根据 wrist 到 middle MCP 的方向生成旋转手部 ROI，裁成 224x224 后送入 landmark 模型。
- landmark 输出 21 个三维相对关键点，使用仿射矩阵映射回原图归一化坐标。

手腕点使用第 0 个关键点。RKNN 输出的相对 `z` 不作为真实距离使用，米制三维位置统一从对齐深度图查询。

### RGB-D 空间点

`VideoSource::QueryPoint3D` 用于把图像坐标转成相机坐标系下的米制 3D 点。

- Orbbec 输入优先使用 SDK 的 D2C 对齐深度流。
- 若设备不直接提供 D2C profile，则使用 Orbbec SDK 做 depth-to-color transformation。
- 查询深度时从小到大扩大邻域，优先选择靠近相机的有效深度，降低空洞和背景混入影响。
- 有 Orbbec 标定参数时调用 SDK 的 `calibration2dTo3d`；没有标定参数时退回到简化反投影。

目标使用检测框中心点查询 3D 坐标。手部查询 wrist、palm 和 21 个关键点；手心点无深度时，会继续查询掌部关键点和手框内部采样点。21 点使用较小深度邻域，避免指尖深度空洞时误取远处背景。

### 手部三维骨骼约束

`HandSkeletonConstraint` 是独立运动学模块，不依赖 RKNN、OpenCV 或 Orbbec SDK：

- 原始深度点和约束后点分别保存在 `measured_position`、`constrained_position`，便于排查误差来源。
- 每根骨骼独立收集有效长度并取中位数，不要求 21 点同时有深度。
- 已有初始骨长后拒绝明显长度离群值；单帧骨段方向翻转超过 120 度时按误检处理。
- 使用轻量 PBD 迭代恢复固定骨长，并限制遮挡点出现不合理反折。
- 深度短时缺失时沿用上一帧受约束姿态，超过最大预测帧数后将该点置为无效。
- 双手按手框 IoU 和中心距离做全组合最小代价匹配，检测顺序交换时保持各自历史。
- 输出五根手指的单位方向向量及掌面法向量。

画面中黄色点表示约束后有有效三维点，橙色点表示遮挡预测点，蓝色点表示没有有效三维点。即使3D到彩色图投影失败，也会使用二维关键点显示，并不会把有效3D误判成蓝色点。手指方向仍保存在结果数据中，不在画面绘制箭头。

### SOP 状态机

`SopStateMachine` 是规则层，配置结构在 `SopStepConfig`：

- `required_objects`：当前步骤必须出现的目标及最小数量。
- `min_stage_sec`：步骤最小驻留时间，防止刚切换步骤时被上一帧残留结果误确认。
- `min_confirm_frames`：连续满足多少帧后推进步骤。
- `timeout_sec`：步骤超时时间，超过后生成 warning。
- `max_hand_object_distance_m`：手腕到目标中心的最大 3D 距离，0 表示关闭。

状态机按 `current_step_index` 顺序推进。每帧会清空上一帧告警，只保留当前帧产生的预警。

## 编译

RK3588 板端需要先安装 OpenCV 和 RKNN runtime。Orbbec SDK 可放在 `third_party/orbbec_sdk`，也可以放在 `/opt/orbbec/OrbbecSDK_v*/SDK`，编译脚本会自动查找。

```bash
./scripts/build.sh
```

常用参数：

```bash
./scripts/build.sh --debug
./scripts/build.sh --orbbec
./scripts/build.sh --clean
```

编译产物固定复制到：

```text
output/rk3588_sop
```

## 运行

默认配置运行：

```bash
./output/rk3588_sop config/sop_config.txt
```

如果使用本地视频，把 `config/sop_config.txt` 改成：

```text
input.type=video
input.uri=config/demo.mp4
```

如果使用 Gemini RGB-D 相机，保持：

```text
input.type=orbbec
```

按 `Esc` 退出窗口。没有桌面环境或 OpenCV 窗口初始化失败时，程序会切到日志模式。

## SOP 工作台页面

页面入口为 [`web/frontend/index.html`](web/frontend/index.html)。前端、后端控制服务和 MediaMTX 分别位于 `web/frontend/`、`web/backend/` 和 `web/mediamtx/`。

在工程根目录启动静态预览：

```bash
python3 -m http.server 4173
```

然后打开 `http://127.0.0.1:4173/web/frontend/index.html`。算法启停由 `web/backend/backend.py` 提供的 API 控制。

完整网页工作流需要同时启动三个服务（建议分别打开终端）：

```bash
./scripts/run_mediamtx.sh
./scripts/run_backend.sh
python3 -m http.server 4173
```

然后访问 `http://设备IP:4173/web/frontend/index.html`，点击“启动算法”。网页播放的是
MediaMTX 的 WebRTC 算法结果流（端口 `8889`），不是本地 MP4 演示画面。

## 配置说明

主配置文件是 `config/sop_config.txt`。

```text
input.type=orbbec
input.width=640
input.height=480
input.fps=30

detector.backend=rknn
detector.model_path=models/yolov8.rknn
detector.conf_threshold=0.25
detector.iou_threshold=0.45
detector.input_size=640

hand.backend=rknn
hand.model_path=models/hand_detector.rknn
hand.landmark_model_path=models/hand_landmarks.rknn
hand.max_num_hands=2
hand.constraint.enabled=true
hand.constraint.calibration_frames=45
hand.constraint.smoothing=0.35
hand.constraint.max_prediction_frames=8
```

SOP 步骤格式：

```text
step.N=id|name|required_objects|hand_roi|min_frames|timeout_sec|warning|max_3d_distance_m|min_stage_sec
```

`required_objects` 支持 `label` 或 `label:count`，后者用于数量校验。

示例：

```text
step.0=base_frame_joint|安装底座与骨架连接处|base:1,frame:1|joint_area|8|10|未检测到底座/骨架或手部未进入连接区域|0|0.8
```

当前主流程会直接读取配置里的 `steps` 和 `rois`，按 `config/sop_config.txt` 修改即可生效。

## 模型文件

默认模型路径：

```text
models/yolov8.rknn
models/hand_detector.rknn
models/hand_landmarks.rknn
```

## 训练到部署

目标检测训练和导出代码放在 `third_party/ultralytics_yolov8_rknn`。这份代码基于 Ultralytics YOLOv8，并加入了 RKNPU 适配导出逻辑。更详细的导出说明见：

```text
third_party/ultralytics_yolov8_rknn/RKOPT_README.zh-CN.md
```

推荐流程：

1. 在训练机准备 Python 环境，并安装 `third_party/ultralytics_yolov8_rknn` 需要的依赖。
2. 按 YOLO 数据集格式准备图片、标签和 data yaml。
3. 使用 Ultralytics 训练检测模型，得到 `.pt` 权重。
4. 导出 RKNN 优化版 ONNX。这个步骤必须从 `.pt` 权重导出，不能直接复用普通 YOLOv8 单输出 ONNX 做 INT8 量化：

```bash
python3 scripts/export_ai_sop_rknnopt_onnx.py \
  --weights models/ai_sop_best.pt \
  --output models/ai_sop_best_rknnopt.onnx
```

5. 使用 RKNN-Toolkit2 将优化版 ONNX 转成 RK3588 可运行的 INT8 `.rknn`。量化时可使用 `models/calibration/dataset.txt` 指定校准图片：

```bash
python3 scripts/export_ai_sop_rknn_int8.py \
  --onnx models/ai_sop_best_rknnopt.onnx \
  --output models/ai_sop_best_int8.rknn \
  --dataset models/calibration/dataset.txt
```

6. 把生成的 RKNN 模型放到 `models/`，例如：

```text
models/ai_sop_best_int8.rknn
```

7. 同步修改 `config/sop_config.txt`：

```text
detector.model_path=models/ai_sop_best_int8.rknn
detector.labels=cover_cloth,long_handle,manual,padding_board,small_red_lever,top_pad,vertical_support_bracket
```

8. 在 RK3588 板端编译并运行：

```bash
./scripts/build.sh
./output/rk3588_sop config/sop_config.txt
```

`models/ai_sop_best.pt`、`models/ai_sop_best.onnx`、`models/ai_sop_best.rknn` 这类中间权重不再固定保留在仓库里。后续训练产生的新权重按实际部署模型名放入 `models/`，再更新配置文件即可。

## 退出码

- `0`：SOP 状态机完成。
- `1`：配置、模型或输入初始化失败。
- `2`：视频流结束、用户按 `Esc` 退出或检测中断，但 SOP 未完成。当前默认检测模式下通常会以 `2` 结束。
# Web 访问与录像

启动 `scripts/run_backend.sh` 后，浏览器直接访问 `http://设备IP:8080/`（只需要 IP 和端口）。工作台的“录制内容”可选择算法处理视频或原始视频，录像由 GStreamer `mpph264enc` 硬件编码保存到 `output/recordings/*.mp4`。
