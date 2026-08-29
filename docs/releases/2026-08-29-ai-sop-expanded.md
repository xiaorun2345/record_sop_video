# 2026-08-29 AI SOP 扩展分支

## 分支用途

这是在 `rgbd-baseline-2026-08-21` 之上继续演进的一条分支，重点是把 SOP 链路补完整并把现成可跑的模型、脚本和部署资源一并整理到仓库里。

## 这个分支做了什么

- 保留 RK3588 上的 YOLOv8 + 手部检测主链路。
- 引入手部三维骨骼约束、遮挡恢复和手指方向输出。
- 把每帧耗时拆成 `read / yolo / hand / 3d / state / draw / process` 等项，便于现场定位瓶颈。
- 增加 SOP 状态机、ROI、串口报警灯和手部约束模块。
- 补齐板端构建脚本、部署脚本和运行库拷贝流程。
- 加入独立的 Orbbec 1080p 硬件编码录像程序。
- 整理 RKNN 模型和校准资源，包含 INT8 检测模型。
- 新增 Web 工作台入口，方便离线查看流程状态。

## 当前包含的模型

- `models/ai_sop_best_int8.rknn`
- `models/yolov8.rknn`
- `models/yolov8_coco80.rknn`
- `models/hand_detector.rknn`
- `models/hand_landmarks.rknn`

## 运行方式

```bash
./scripts/build.sh
./output/rk3588_sop config/sop_config.txt
```

默认配置使用：

- `input.type=video`
- `input.uri=recordings/orbbec_20240825_195251_973.mp4`
- `detector.model_path=models/ai_sop_best_int8.rknn`
- `hand.model_path=models/hand_detector.rknn`
- `hand.landmark_model_path=models/hand_landmarks.rknn`

## 目前观察到的耗时

在本地视频上跑到的采样里，整体单帧处理大约 40-70ms，前几帧 warmup 更高。主要时间花在：

- YOLO 总耗时
- 手部检测总耗时
- 手部 landmark 的重复推理

NPU 纯推理时间只占其中一部分，CPU 侧预处理、后处理和每只手的循环开销也很明显。

## 部署资源

- `deploy/run_sop.sh`
- `deploy/check_runtime.sh`
- `deploy/install_permissions.sh`
- `standalone_camera/orbbec_1080p.cpp`
- `standalone_camera/build.sh`
- `standalone_camera/bin/orbbec_1080p`
- `third_party/rknn_runtime/`
- `third_party/orbbec_sdk/`

## 备注

这个分支适合板端直接验证和继续迭代。如果后续要回到更保守的状态，可以对照 `docs/releases/2026-08-21-rgbd-baseline.md`。
