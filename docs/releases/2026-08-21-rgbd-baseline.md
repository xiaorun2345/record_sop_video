# 2026-08-21 RGB-D 基线版本

## 版本用途

这是加入“手部 21 点真实 3D、骨骼长度约束、遮挡恢复和手指方向”之前的稳定保存点。后续相关改造出现问题时，应优先回退到本版本，而不是手工删除新增代码。

建议 Git 标签：

```text
rgbd-baseline-2026-08-21
```

## 当前功能

- RK3588 板端直接编译，产物固定为 `output/rk3588_sop`。
- YOLOv8 RKNN COCO 目标检测和检测框坐标恢复。
- RKNN 手掌检测及 21 个二维手部关键点。
- 独立 `ObjectTracker`，使用匈牙利算法按类别和 IoU 匹配目标。
- 支持本地视频、普通摄像头、GStreamer 和 Orbbec Gemini RGB-D 输入。
- Orbbec SDK 优先使用 D2C；没有匹配 profile 时使用 SDK depth-to-color transformation。
- 目标框中心、手腕和手心的米制 3D 查询。
- 手心深度无效时，依次使用掌部关键点和手框内部网格采样。
- 画面显示目标位置、手部关键点、手心查询状态和每个处理阶段耗时。
- 画面标题为 `RK3588 RGB-D Perception`。

## 当前运行配置

默认配置文件：`config/sop_config.txt`

```text
input.type=orbbec
input.width=640
input.height=480
input.fps=30

detector.model_path=models/yolov8.rknn
detector.input_size=640
detector.conf_threshold=0.65

hand.model_path=models/hand_detector.rknn
hand.landmark_model_path=models/hand_landmarks.rknn
hand.max_num_hands=2
```

`main.cpp` 当前会清空 `steps` 和 `rois`，因此实际运行的是 RGB-D 感知模式，SOP 状态机代码仍保留但没有启用。

## 编译与运行

```bash
./scripts/build.sh --clean
./output/rk3588_sop config/sop_config.txt
```

编译依赖：

- RKNN Runtime
- OpenCV
- `third_party/orbbec_sdk`

## 模型校验值

用于确认回退后模型没有被替换：

```text
defa25aea179be4da5c5c5826e0be26833b9f818f86b6519620f52f6df3b6a17  models/yolov8.rknn
69261a04b004ffdfb39ea4b98a0cd5ab5f68d66649e8c7f860ea02fd8347bfd5  models/hand_detector.rknn
d9ed3b29476febc0429c10c7e16f2369a158f51c0cd3289937fa04b8914064c4  models/hand_landmarks.rknn
```

## 已知边界

- 21 个手部关键点当前只有模型输出的二维归一化坐标和相对 `z`，尚未逐点查询米制 3D。
- 当前真实 3D 只包含目标中心、手腕和手心。
- 尚未实现固定骨长、关节角度约束、遮挡恢复、手指方向和掌面法向量。
- 普通视频和普通 USB 摄像头没有深度数据，会继续运行二维检测，但不会产生有效 3D。
- 当前默认关闭 SOP/ROI 规则，只运行感知和可视化流程。

## 回退方法

标签创建并推送到 GitHub 后，可在当前工作目录回退：

```bash
git fetch --tags
git switch -c restore-rgbd-baseline rgbd-baseline-2026-08-21
./scripts/build.sh --clean
```

建议先创建新分支验证，不要直接覆盖正在开发的分支。确认无误后再决定是否让主分支指向这个版本。

查看该基线内容但不切换分支：

```bash
git show rgbd-baseline-2026-08-21:docs/releases/2026-08-21-rgbd-baseline.md
```
