# 05 系统设置

## 页面目标

独立管理 RK3588 本机的网络、设备身份、时间、存储、服务端口、日志和维护设置。系统设置不负责 SOP 业务规则，也不暴露声光报警协议细节。

## 页面布局

- 左侧：系统设置子菜单。
- 主区：设备身份、网络配置、时间与区域、存储策略、服务配置、日志设置、维护操作。
- 右侧或顶部：当前设备在线状态、未保存变更提示和应用按钮。

## 网络配置

- 有线网络和 Wi-Fi 标签页。
- DHCP / 静态 IP 切换。
- 静态 IP、子网掩码、网关、DNS、MAC 地址。
- 当前连接状态、链路速度和网络连通性测试。
- 修改 IP 前弹出确认，提示浏览器将切换到新地址。
- 保存后先验证配置，再应用；失败自动恢复原配置。

## 设备身份与时间

- 设备名称、设备编号、工位编号和主机名。
- 时区、手动时间、NTP 开关、NTP 服务器和同步状态。

## 存储与服务

- 显示系统、模型、作业数据和日志占用。
- 自动清理天数、视频保留开关、磁盘低空间阈值。
- Web/API/Node-RED 端口只对管理员开放修改。
- 服务重启按钮需要确认，并显示重启影响。

## 外设状态

声光报警器只在这里显示状态，不做复杂配置：

- 连接状态：已连接/未连接。
- 设备路径：`/dev/ch341-light`。
- 波特率：`9600（固定）`。
- 通信状态、最后心跳、测试按钮。
- 具体触发条件和报警持续时间在“​​SOP 配置 → 异常联动”中设置。

## 维护操作

- 导出配置备份。
- 导入配置并预览变更。
- 恢复最近备份。
- 清理缓存和日志。
- 恢复出厂设置，必须输入设备管理员密码并二次确认。

## 建议接口

- `GET/PUT /api/device/identity`。
- `GET/PUT /api/network`、`POST /api/network/test`。
- `GET/PUT /api/time`、`GET/PUT /api/storage-policy`。
- `GET/PUT /api/services/config`、`POST /api/services/restart`。
- `GET /api/peripherals/alarm-light`、`POST /api/peripherals/alarm-light/test`。
- `POST /api/config/backup`、`POST /api/config/restore`、`POST /api/system/factory-reset`。
