# RTSP 断连恢复实现与 PC 端到端验证交接

更新日期：2026-09-12

## 1. 当前结论

RTSP 输出已实现**进程内断连自动恢复**，并在 PC 完成双场景端到端验证：

1. **运行中断连恢复**：`required=true` 的 RTSP 输出在服务器消失后不再停止整条管道；Sink
   按配置间隔自动重建 RTSP 会话，服务器恢复后继续推流。场景 A 中接收端断开约 3.5 秒，
   网关自动重连并成功推送第二段流，全程退出码 0。
2. **冷启动等待**：`required=false` 的输出开启重连后，服务器晚于网关启动也能先进入等待
   状态，服务器出现后自动开流。场景 B 中网关先运行 4 秒，接收端启动后成功获得完整流。
3. **默认行为不变**：`reconnect_interval_ms` 默认 0（关闭重连）时，完全保留原有语义
   （可选输出失败即隔离、必需输出失败即停止）。

验证结果：Ubuntu 24.04 + FFmpeg 6.1.1 原生构建 **68/68 测试通过**；两段场景共三份捕获
MP4（4.021 秒 / 4.362 秒 / 4.946 秒）均为 H.264 640x360 + AAC 48 kHz 双声道；无残留网关进程。

## 2. 设计与实现

### 2.1 Router 的可重试错误语义

`PacketRouter` 原先对写入失败只有两种处理：必需输出致命停止、可选输出永久隔离。断连恢复
需要第三种语义“暂时失败但可恢复”，因此：

- `Error::retryable` 为 true 时，worker 保持运行、队列保持打开，不设致命错误、不隔离；
- 错误计数与日志按 1 秒节流（`sink_write_retry`），避免断连期间按包刷屏；
- worker 健康状态在可重试错误期间标记为 `kDegraded`，恢复后的首次成功写入转回
  `kRunning` 并记录 `sink_write_recovered`；
- 可重试错误不计入 `packets_consumed`，断连期间的丢包可从 routed/consumed 差值、
  `errors_total`（每秒一次）和 Sink 队列 dropped 指标观察。

### 2.2 RTSP Sink 会话状态机

`FfmpegRtspSink` 把原来一次性的 Open 拆成可重复的会话建立：

- `Connect()`：新建格式上下文、添加音视频流、写流头（ANNOUNCE），失败时释放半初始化资源；
- 写入失败视为会话级故障：释放当前会话，若开启重连则按间隔调度下一次 `Connect()`；
- 重连未到期时的写入直接返回可重试错误（本包按丢弃处理），不阻塞后续包；
- `Flush()` 在断连状态下直接结束，不尝试写 trailer；
- 重连成功记录 `rtsp_reconnected`，断连记录 `rtsp_connection_lost`，
  冷启动等待记录 `rtsp_waiting_for_server`。

### 2.3 配置项

| 项目 | 说明 |
|---|---|
| 字段 | `reconnect_interval_ms`（整数，毫秒） |
| 范围 | `[0, 60000]`，0 表示关闭重连（默认） |
| 适用范围 | 仅 RTSP 输出；其他输出携带非 0 值会在启动前被配置校验拒绝 |
| 启动语义 | `required=true` 首次连接失败仍然快速失败；`required=false` 且开启重连时先进入等待 |
| 示例 | `config/mock-ffmpeg-rtsp.json` 已启用 1000 ms |

## 3. 验证证据

| 项目 | 值 |
|---|---|
| 源码归档 | `rkav-rtsp-reconnect-source-20260912-1623.tar.gz`，SHA-256 `60ffb35e...3d9ae` |
| 构建与测试 | 原生 x86_64 Debug；`ctest` 68/68 通过（含 3 项 Router、3 项配置、3 项重连 Sink 测试） |
| 场景 A 配置 | `required=true`，`reconnect_interval_ms=500`，时长 24 秒 |
| 场景 A 结果 | `rtsp_connection_lost`×2、`rtsp_reconnected`×1、`sink_write_retry`×16、`sink_write_recovered`×1；网关退出码 0；捕获 4.021 秒 + 4.362 秒 |
| 场景 B 配置 | `required=false`，`reconnect_interval_ms=500`，时长 14 秒 |
| 场景 B 结果 | `rtsp_waiting_for_server`×1、`rtsp_reconnected`×1；网关退出码 0；捕获 4.946 秒 |
| 证据目录 | 共享目录 `rtsp-reconnect-e2e-20260912-162331`（Windows 侧 `D:\share\` 同名目录），含日志、捕获文件、用到的配置和 SHA256SUMS |

关键捕获文件 SHA-256：`a-capture1.mp4` `6f924fd3...e5907`、`a-capture2.mp4` `df529a61...f4ef5`、
`b-capture.mp4` `7ee4c79c...33b11`。

## 4. 已知边界

- 重连尝试在 worker 线程内同步执行；目标服务器“黑 hole”（不回 RST）时 TCP 连接可能阻塞到
  操作系统超时。本阶段只验证了回环地址的快速失败/恢复，连接超时参数化是后续工作。
- 直播语义：断连期间不缓存、不重传媒体包，恢复后直接从后续 GOP 继续。
- `required=true` 的启动失败语义保持不变；systemd 自动拉起仍作为启动期兜底。
- 板端 RTSP 验证仍需等待 MPP H.264 编码通过；本次验证使用 FFmpeg 软件编码。
- 未验证 UDP 传输、鉴权、TLS 和跨网段丢包场景下的重连行为。

## 5. 涉及名词

- **retryable 错误**：错误分类中的可恢复标志；表达“本次失败但可通过退避重试恢复”，
  与“隔离/致命”区分开。
- **会话重建**：RTSP 推流是一次 ANNOUNCE/RECORD 会话；断连后必须重新写流头，不能复用旧
  上下文，因此重连即完整重建 muxer 会话。
- **退避间隔**：两次重连尝试之间的最小时间，避免断连期间按包高频重试。
- **可重试错误节流**：Router 对同一条可重试错误按 1 秒聚合计数和日志，保证指标可读且不刷屏。

## 6. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [RTSP 输出 PC 端到端验证与实现修正交接](68-RTSP输出PC端到端验证与实现修正交接.md)
- [RTSP 网络输出模块实现与 systemd 服务优化](65-RTSP网络输出模块实现与systemd服务优化.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md) 与 [通俗版](07-项目问题汇总-通俗版.md)，P117
