# SOP 配置与工作台合并

当前工程只运行一个 FastAPI/Uvicorn 服务（默认 `8080`）。SOP 配置页面是 Vue 路由 `/sop-config`，与工作台、设备管理和系统设置共用同一前端构建和同一后端进程。

## 摄像头与视频流

SOP 页面只使用 `/api/camera/status`、`/api/camera/start` 和 `/api/camera/stop`，视频通过现有 MediaMTX 的 `raw` WebRTC WHEP 流显示；工作台使用同一 MediaMTX 实例的 `raw`/`sop` 路径。页面切换不会启动第二个采集进程，也不会启动上层 `/home/armsom/SOP` 的 8000 服务。

## 判定条件链路

1. 在 `/sop-config` 配置步骤、必检对象、数量和 ROI。
2. “保存并发布”调用 `/api/sops/{id}/publish`。
3. 后端将发布版本保存为 `config/active_sop_judgement.json`，并把同一规则翻译为 C++ `config/sop_config.txt` 的 `roi.*`/`step.*` 条目。
4. 工作台轮询 `/api/sop/runtime`，显示当前已发布 SOP 和必检对象条件。
5. 下一次启动算法时，C++ 进程从翻译后的配置加载条件，使用现有 `SopStateMachine` 连续确认帧、数量、超时和空间约束进行触发判定。

草稿不会被工作台使用；未配置必检对象的步骤无法发布，避免生成永远无法触发的规则。
