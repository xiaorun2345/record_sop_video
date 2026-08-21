# RK3588 SOP 检测示例

这个工程用于在 RK3588 上跑一条装配场景检测链路：视频或 Gemini RGB-D 相机输入，YOLOv8 目标检测，手部检测与 21 点关键点检测，最后把检测结果、深度信息、耗时数据画到画面上并输出日志。

当前可回退基线版本说明：[`docs/releases/2026-08-21-rgbd-baseline.md`](docs/releases/2026-08-21-rgbd-baseline.md)。该版本是加入手部 21 点三维骨骼约束之前的稳定保存点。

当前 `main.cpp` 默认清空了配置里的 `steps` 和 `rois`，实际运行模式是检测与可视化链路。SOP 状态机、ROI、超时和 3D 距离规则代码已经保留在工程内，恢复配置后即可接回步骤判断。

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
5. 对目标框中心、手腕和手心查询 3D 坐标。
6. 把结果交给状态机和可视化模块，输出窗口画面、日志、可选结果视频。

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

手腕点使用第 0 个关键点。可视化时会画出手部框、21 点和骨架连接线。

### RGB-D 空间点

`VideoSource::QueryPoint3D` 用于把图像坐标转成相机坐标系下的米制 3D 点。

- Orbbec 输入优先使用 SDK 的 D2C 对齐深度流。
- 若设备不直接提供 D2C profile，则使用 Orbbec SDK 做 depth-to-color transformation。
- 查询深度时从小到大扩大邻域，优先选择靠近相机的有效深度，降低空洞和背景混入影响。
- 有 Orbbec 标定参数时调用 SDK 的 `calibration2dTo3d`；没有标定参数时退回到简化反投影。

目标使用检测框中心点查询 3D 坐标。手部查询 wrist 和 palm；手心点无深度时，会继续查询掌部关键点和手框内部采样点。

### SOP 状态机

`SopStateMachine` 是规则层，配置结构在 `SopStepConfig`：

- `required_objects`：当前步骤必须出现的目标类别。
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
```

SOP 步骤格式：

```text
step.N=id|name|required_objects|hand_roi|min_frames|timeout_sec|warning|max_3d_distance_m|min_stage_sec
```

示例：

```text
step.0=base_frame_joint|安装底座与骨架连接处|base,frame|joint_area|8|10|未检测到底座/骨架或手部未进入连接区域|0|0.8
```

注意：当前主程序默认执行检测链路，`main.cpp` 中会清空 `steps` 和 `rois`。需要启用 SOP 步骤判断时，删除这两行：

```cpp
config.steps.clear();
config.rois.clear();
```

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
4. 修改 `third_party/ultralytics_yolov8_rknn/ultralytics/cfg/default.yaml` 中的 `model` 路径，指向训练得到的 `.pt`。
5. 在 `third_party/ultralytics_yolov8_rknn` 目录执行 ONNX 导出：

```bash
python ./ultralytics/engine/exporter.py
```

6. 使用 RKNN-Toolkit2 将 ONNX 转成 RK3588 可运行的 `.rknn`。量化时可使用 `models/calibration/dataset.txt` 指定校准图片。
7. 把生成的 RKNN 模型放到 `models/`，例如：

```text
models/yolov8.rknn
```

8. 同步修改 `config/sop_config.txt`：

```text
detector.model_path=models/yolov8.rknn
detector.labels=base,frame,mirror,screw
```

9. 在 RK3588 板端编译并运行：

```bash
./scripts/build.sh
./output/rk3588_sop config/sop_config.txt
```

`models/ai_sop_best.pt`、`models/ai_sop_best.onnx`、`models/ai_sop_best.rknn` 这类中间权重不再固定保留在仓库里。后续训练产生的新权重按实际部署模型名放入 `models/`，再更新配置文件即可。

## 退出码

- `0`：SOP 状态机完成。
- `1`：配置、模型或输入初始化失败。
- `2`：视频流结束、用户按 `Esc` 退出或检测中断，但 SOP 未完成。当前默认检测模式下通常会以 `2` 结束。
