# Orin NX 双路 RTSP 事件视频过滤系统设计

## 1. 目标

在 NVIDIA Jetson Orin NX 上接入两路固定机位的 1920×1080 RTSP 视频。系统仅在检测到人员且手部较大比例进入固定工作区时保存原始视频；手离开工作区持续一段时间后停止保存。

系统优先保证关键操作不漏录，允许少量冗余录像。模型使用公开预训练权重并通过 TensorRT 推理，不要求重新采集和标注训练集。C++ 负责拉流、推理调度、几何判断、状态管理和文件落盘。

## 2. 范围与约束

- 输入：两路 RTSP，1920×1080；设计默认 25 FPS，实际帧率从码流读取。
- 摄像头和工作区位置固定，每路摄像头分别配置多边形 ROI。
- 保存未绘制检测框的原始 H.264/H.265 视频。
- FFmpeg 负责 RTSP、Orin 硬件解码、压缩包缓存和 MP4 封装。
- TensorRT 负责人员检测和人体/手部关键点推理。
- 从硬解输出表面到 TensorRT 输入张量的所有像素操作必须在 GPU 完成；禁止将视频帧下载到 CPU 内存。
- 录像帧率与原始码流一致；检测支路允许抽帧和丢弃过期解码帧。
- 初版不识别拿取、放置、拧紧等语义动作。

## 3. 总体架构

每路摄像头使用彼此隔离的输入与录像状态，共享只读 TensorRT Engine；每路使用独立 TensorRT execution context 和 CUDA stream。

```text
RTSP demux
 ├─ 压缩包支路 → GOP 环形缓存 → 事件 MP4 muxer
 └─ 解码支路 → FFmpeg/NVIDIA 硬解 → NV12设备表面
                                      ↓ CUDA互操作
                                  GPU RGB帧
                                      ↓ CUDA裁剪/缩放/归一化
                               TensorRT 人员检测
                                      ↓ GPU裁剪/缩放/归一化
                         TensorRT WholeBody/手关键点
                                      ↓ 小型检测结果回传CPU
                               ROI判断与事件状态机
```

压缩包支路不可因推理速度下降而主动丢包。解码支路只保留最新待检测帧，可以覆盖过期帧，防止推理积压影响实时性。

除最终的检测框、关键点、置信度和时间戳外，任何视频像素、中间图像和模型输入张量都不得进入 CPU 内存。

## 4. GPU 全链路像素处理

### 4.1 硬件解码与 CUDA 互操作

FFmpeg 使用 Jetson 平台的 NVIDIA V4L2 硬件解码器处理 H.264/H.265。硬解输出保持为 NV12 设备表面，不使用 `av_hwframe_transfer_data()`，也不转换成普通 CPU `AVFrame`。

实现时从解码表面取得可互操作的 NVMM/DMA-BUF 句柄，通过 EGLImage/CUDA interop 或目标 JetPack 实际提供的等价接口映射为 CUDA 可访问表面。映射对象按解码缓冲池生命周期复用，避免逐帧注册、注销和分配。

正式开发前必须先做一项平台探针：锁定 Orin NX 的 JetPack/L4T/FFmpeg 版本，并验证所用 FFmpeg 解码器确实可以暴露 NVMM/DMA-BUF 设备表面。如果发行版 FFmpeg 只能输出 CPU YUV，则需要使用 NVIDIA Jetson FFmpeg 补丁/解码接口构建 FFmpeg；不能以 CPU 下载作为降级方案。

### 4.2 NV12 到 RGB

颜色转换使用 CUDA kernel 在设备端完成：

- 输入：NV12 Y 平面和交错 UV 平面；
- 输出：预分配的 RGB8 GPU 缓冲；
- 色彩矩阵根据流信息选择 BT.601 或 BT.709；1080p 未携带可靠元数据时默认 BT.709；
- limited/full range 根据码流元数据处理，不能固定假设；
- kernel 支持 pitch，不假设图像行连续；
- 每路使用固定缓冲池，不进行逐帧 `cudaMalloc/cudaFree`。

若模型仅消费当前帧，RGB 缓冲采用双缓冲或三缓冲，并通过 CUDA event 保护生产者与消费者生命周期。

### 4.3 GPU 裁剪、缩放和张量化

人员检测预处理在 CUDA 上完成：RGB8 全帧按比例缩放并 letterbox 到模型输入，随后完成 RGB 排列、FP16/FP32 转换、归一化及 HWC→CHW，直接写入 TensorRT input binding 的设备地址。

姿态模型预处理也在 CUDA 上完成：根据人员框在 RGB GPU 帧上执行边界裁剪、仿射变换、缩放、归一化及 HWC→CHW，直接写入姿态模型输入张量。裁剪框坐标由 CPU 检测结果生成，但裁剪像素本身不经过 CPU。

两路流分别使用独立 CUDA stream。推荐顺序为：

```text
NV12→RGB kernel
  → detector preprocess kernel
  → TensorRT detector enqueueV3
  → 复制少量检测结果到 pinned host memory
  → pose preprocess kernel
  → TensorRT pose enqueueV3
  → 复制少量关键点结果到 pinned host memory
```

仅在 CPU 确实需要解析输出框来决定姿态裁剪时进行 stream event 同步。禁止在正常帧路径中使用全设备 `cudaDeviceSynchronize()`。

## 5. 模型方案

### 5.1 人员检测

使用带公开预训练权重的 RT-DETR-R18，只保留 `person` 类。模型导出为 ONNX，再构建 FP16 TensorRT Engine。初始输入尺寸为 640×640，保持宽高比并使用 letterbox。

人员检测默认按每路 5 FPS 调度。没有人员与“工作区扩展区域”相交时，不运行后续关键点模型。

### 5.2 手部关键点

使用公开预训练的 RTMPose/RTMW WholeBody 模型，通过人员检测框得到人体裁剪，输出包含双手在内的 WholeBody 关键点。模型使用 FP16 TensorRT Engine。

初版优先选择中小尺寸模型并进行现场无训练验证。如果 WholeBody 模型对画面中的手尺度过小，可在不改变系统接口的情况下替换为“预训练手部检测器 + RTMPose-Hand”；该替换不改变 ROI 状态机和录像模块。

关键点模型默认按每路 8–12 FPS 调度，只处理检测框与工作区扩展区域相交的人员。工作区扩展区域是 ROI 外扩约 10%，用于提前获得手部轨迹，避免进入边界时才首次推理。

## 6. 手进入工作区判定

工作区 ROI 为图像坐标中的多边形。每只手独立计算：

- `valid_count`：置信度不低于 0.45 的手部关键点数量；
- `inside_count`：有效且位于 ROI 内的关键点数量；
- `inside_ratio = inside_count / valid_count`。

初始进入条件：

- 画面中存在与工作区扩展区域相关的人员；
- 某只手 `valid_count >= 12`；
- 该手 `inside_ratio >= 0.60`；
- 条件连续成立 0.5 秒。

录像保持条件采用滞回阈值，避免边界抖动：某只手至少有 6 个有效关键点位于 ROI 内，即认为该手仍在操作区域。所有手均不满足保持条件时开始计算离开时间。

所有阈值均写入每路摄像头配置文件，现场验证后只调整配置，不重新训练模型。

## 7. 录像状态机

状态定义：

```text
IDLE → ENTER_PENDING → RECORDING → LEAVE_PENDING → FINALIZE → IDLE
```

- `IDLE`：维护压缩包环形缓存，不创建事件文件。
- `ENTER_PENDING`：进入条件开始成立；若在 0.5 秒内失效则回到 `IDLE`。
- `RECORDING`：打开临时事件文件，先写入触发前缓存，再持续写入实时压缩包。
- `LEAVE_PENDING`：所有手离开；8 秒内重新满足保持条件则回到 `RECORDING`。
- `FINALIZE`：写完容器尾部并原子改名，然后回到 `IDLE`。

默认触发前缓存为 5 秒，离开超时为 8 秒，最短事件长度为 10 秒。相邻事件间隔不足 5 秒时，由离开滞回自然合并为同一文件。

## 8. 原码流环形缓存与封装

环形缓存保存 `AVPacket` 的深拷贝及必要的流参数，不保存解码图像。缓存边界按 GOP 管理：仅从可独立解码的关键帧开始保留，容量至少覆盖配置的触发前时长，并设置额外一个 GOP 的余量。

触发时选择目标时间之前最近的有效关键帧作为事件起点。写入 MP4 时：

- 按输入 time base 重标定到输出 time base；
- 以首个写入包的 DTS/PTS 为基准归零；
- 保持 DTS 单调递增；
- 复制 codec parameters，不解码和重新编码；
- 初版只保存视频轨；若后续需要音频，音视频必须共用事件时间轴并从可用边界开始写入。

临时扩展名采用 `.part.mp4`，封装正常结束后改名为 `.mp4`。为增强异常断电可恢复性，MP4 使用 fragmented MP4 参数；事件索引只记录最终成功关闭的文件。

## 9. FFmpeg 与 RTSP 策略

- RTSP 默认优先 TCP，减少无线或复杂网络环境中的花屏和包丢失。
- 设置连接和读超时，避免阻塞线程永久挂起。
- 网络错误后指数退避重连，并设置最大退避时间。
- 重连成功后清空旧 GOP 缓存，等待新关键帧建立可解码边界。
- 输入分辨率、编码格式或时间戳发生不连续变化时，安全结束当前事件并重建该路管线。
- NVIDIA V4L2 硬件解码用于推理支路；解码输出不得经过 CPU YUV/RGB 帧。
- 原始保存支路直接 remux，不调用 NVENC，也不依赖解码支路是否抽帧。

## 10. 并发与资源管理

每路摄像头包含：

- 一个 demux/packet 分发线程；
- 一个硬解与最新帧更新路径；
- 一个受状态机控制的 muxer；
- 独立 packet ring buffer 和时间戳状态。

全局包含一个推理调度器。调度器从两路最新 GPU 帧槽位取样，按时间公平轮询，分别提交到独立 CUDA stream。NV12→RGB、裁剪、缩放、归一化、TensorRT 推理和小型结果拷贝异步执行。

CPU 侧队列只能持有设备表面句柄、CUDA event、时间戳和检测结果，不得持有完整图像副本。所有 GPU 缓冲在初始化或分辨率变更时分配，正常帧路径不得重复申请显存。

优先级从高到低为：RTSP 持续读取与原码流缓存、事件写盘、检测实时性。推理过载时降低检测频率，不影响原视频保存质量。

## 11. 配置与输出

每路摄像头配置包括：RTSP 地址引用、ROI、多边形扩展比例、模型阈值、检测频率和录像时长。凭据不直接写入可提交的普通配置文件，使用环境变量或受限权限的本地 secrets 文件引用。

示例：

```json
{
  "camera_id": "camera_01",
  "rtsp_url_env": "CAMERA_01_RTSP_URL",
  "roi": [[430, 220], [1580, 220], [1750, 930], [280, 930]],
  "person_fps": 5,
  "pose_fps": 10,
  "keypoint_confidence": 0.45,
  "enter_valid_points": 12,
  "enter_inside_ratio": 0.60,
  "hold_inside_points": 6,
  "enter_duration_ms": 500,
  "pre_record_seconds": 5,
  "leave_timeout_seconds": 8,
  "minimum_clip_seconds": 10
}
```

输出路径：

```text
records/<camera_id>/<YYYY-MM-DD>/<start>_<end>.mp4
records/<camera_id>/<YYYY-MM-DD>/<start>_<end>.json
```

JSON 事件元数据记录相机、开始/结束时间、触发阈值、模型版本、平均置信度、录像文件大小和结束原因，不记录 RTSP 密码。

## 12. 故障处理

- RTSP 中断：尽可能完成当前 MP4，标记结束原因为 `stream_disconnect`，随后重连。
- 无关键帧：不得启动新事件文件，继续等待关键帧并记录告警。
- 时间戳回退或大幅跳变：结束事件、清空缓存并重建时间基准。
- 磁盘空间不足：停止创建新事件，保留推理和健康状态；不自动删除历史录像，除非后续明确增加保留策略。
- TensorRT 推理失败：该帧按无有效结果处理，但短时失败不立即停止录像；持续失败触发健康告警。
- CUDA interop 或颜色转换失败：释放该解码表面的引用并记录错误；不得回退到 CPU 图像处理。
- 程序退出：先停止创建新事件，再完成 muxer 尾部，最后释放 FFmpeg、CUDA 和 TensorRT 资源。

## 13. 验证标准

### 功能验证

- 无人画面不产生录像。
- 人员出现但手未进入 ROI 时不产生录像。
- 手满足进入条件后，录像包含触发前约 5 秒内容。
- 手短暂离开不足 8 秒不会切段。
- 手离开超过 8 秒后文件正常关闭并可播放。
- 两路摄像头可同时独立触发和结束。

### 准确性验证

使用现场视频进行零训练阈值校准，至少覆盖裸手、常用手套、不同光照、快速进入、边界操作和人员遮挡。首轮目标是关键事件召回率优先；通过调低进入阈值或延长离开时间容忍少量冗余录像。

### 性能验证

- 两路原码流持续读取和保存无主动丢包。
- 推理队列不累积旧帧。
- 使用 Nsight Systems 或等价工具确认解码后不存在整帧 Device→Host→Device 往返。
- 正常帧路径中不存在 `av_hwframe_transfer_data()`、CPU `sws_scale()`、OpenCV CPU `cvtColor/resize/crop` 或整帧 `cudaMemcpyDeviceToHost()`。
- NV12→RGB、letterbox、裁剪、缩放、归一化均显示为 GPU kernel/VPI CUDA 后端任务，并与 TensorRT 使用明确的 CUDA stream/event 依赖。
- 只允许检测结果和关键点等小型张量回传 CPU；传输量需可由接口尺寸解释。
- 连续运行 24 小时无显存持续增长、文件句柄泄漏或 RTSP 线程卡死。
- 同时触发两路录像时仍能维持配置的检测频率；不能维持时允许自动降频并记录指标。

## 14. 不在初版范围内

- 识别具体 SOP 动作语义或步骤顺序；
- 自动学习工作区；
- 在保存视频中绘制框、关键点或 ROI；
- 云端上传、Web 管理界面和历史录像自动删除；
- 为现场手套或特殊姿态重新标注训练。

