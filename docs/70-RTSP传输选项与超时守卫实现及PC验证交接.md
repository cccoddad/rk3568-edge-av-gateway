# RTSP 传输选项与超时守卫实现及 PC 验证交接

更新日期：2026-09-12

## 1. 当前结论

RTSP 输出补齐两个健壮性能力并完成 PC 端到端验证：

1. **传输协议可选**：新增 `rtsp_transport`（`"tcp"` 默认 / `"udp"`），UDP 场景在回环完成
   4.021 秒 H.264+AAC 捕获验证。
2. **可配置超时守卫**：新增 `rtsp_timeout_ms`（默认 5000，0 关闭），通过
   `AVFormatContext::interrupt_callback` 为**建连、RTSP 握手和单次网络写入**设置截止时间，
   超时返回 `category=timeout` 的明确错误。iptables 黑洞场景中 3000 ms 配置在 **3074 ms**
   精确失败；此前 FFmpeg 的 TCP 建连默认每地址 5 秒，而建连后的握手读取和流式写入
   `rw_timeout` 默认为 -1（无超时），僵死连接会永久阻塞 Sink worker，使断连恢复失效。

回归验证：中途断连自动重连与冷启动等待两个场景保持通过；Ubuntu 24.04 + FFmpeg 6.1.1
原生构建 **73/73 测试通过**。

## 2. 实现说明

### 2.1 传输选项

`Connect()` 中把 `rtsp_transport` 按配置写入 muxer 选项（原实现硬编码 tcp）。RTSP 控制通道
始终是 TCP，选项只影响 RTP 媒体通道；UDP 无重传、延迟更低，TCP 交织更可靠。

### 2.2 为什么用中断回调而不是 URL `?timeout=`

只读核对 FFmpeg 6.1 源码（`libavformat/tcp.c`、`network.c`、`rtsp.c`）：

- TCP 协议 `open_timeout` 默认 5 秒、`rw_timeout` 默认 -1；`?timeout=` 只能同时设置两者，
  且需要 URL 查询参数透传到内部 TCP 连接；
- `ff_connect_parallel` 与 `ff_network_wait_fd_timeout` 都会检查 URLContext 的中断回调，
  而 RTSP 用 `AVFormatContext::interrupt_callback` 打开控制连接；
- 因此把截止时间放在回调里，可以覆盖“建连 + 握手 + 单次写入”，并且能精确区分超时错误，
  不依赖 URL 参数透传。

实现要点：

- `ArmTimeout()` 在每次 `avformat_write_header` 和 `av_interleaved_write_frame` 前记录
  `now + rtsp_timeout_ms`，调用结束后 `DisarmTimeout()`，不影响其他阶段；
- 回调到点置 `interrupted` 并返回 1，FFmpeg 网络等待立即放弃；
- 错误映射为 `ErrorCategory::kTimeout`，消息带 `(timed out after N ms)`；
- 写入超时与写失败同路径处理：结束当前会话，开启重连时按间隔重建。

## 3. 配置语义

| 字段 | 取值 | 默认 | 校验 |
|---|---|---|---|
| `rtsp_transport` | `"tcp"` / `"udp"` | `"tcp"` | 未知取值拒绝；非 RTSP 输出只允许默认值 |
| `rtsp_timeout_ms` | `[0, 60000]`，0 关闭守卫 | `5000` | 超范围拒绝；非 RTSP 输出只允许默认值 5000 |

`config/mock-ffmpeg-rtsp.json` 已显式写入两个字段作为示例。

## 4. 验证证据

| 项目 | 值 |
|---|---|
| 源码归档 | `rkav-rtsp-transport-timeout-source-20260912-1634.tar.gz`，SHA-256 `80a71712...76495` |
| 构建与测试 | 原生 x86_64 Debug；`ctest` 73/73 通过 |
| 场景 A 回归 | 中途断连：`connection_lost`×2、`reconnected`×1，捕获 4.021 秒 + 4.361 秒，退出码 0 |
| 场景 B 回归 | 冷启动等待：`waiting_for_server`×1、`reconnected`×1，捕获 4.924 秒，退出码 0 |
| 场景 C（UDP） | 接收端 `-rtsp_transport udp`，捕获 4.021 秒 H.264 640x360 + AAC 48 kHz 双声道；接收端日志有一次性 UDP 端口 bind 重试警告，捕获完整 |
| 场景 D（黑洞超时） | `iptables DROP` 18555 端口，配置 3000 ms：退出码 1，**3074 ms** 返回，错误 `cannot write RTSP stream header (timed out after 3000 ms): Connection timed out [category=timeout, native_code=-110]` |
| 规则清理 | iptables 规则已删除，无残留网关进程 |
| 证据目录 | 共享目录 `rtsp-transport-timeout-e2e-20260912-163525`（Windows 侧 `D:\share\` 同名目录），含日志、捕获、配置和 SHA256SUMS |

关键捕获文件 SHA-256：`a-capture1.mp4` `71915a15...0e0a8`、`a-capture2.mp4` `303d5d06...4c2fc`、
`b-capture.mp4` `66cbcfec...4b772`、`c-capture.mp4` `99dd9f9f...14bdc`。

## 5. 边界与未完成项

- DNS 解析（`getaddrinfo`）不受中断回调约束，域名服务器黑洞时建连仍可能超过配置超时；
  当前配置使用 IP 地址。
- UDP 验证仅在回环完成，未覆盖跨网段丢包、乱序和 NAT；UDP 本身无重传，恢复语义依赖重连。
- 写入超时中断会丢弃当前会话并走重连路径，属于预期语义（直播不重传）。
- 鉴权和 TLS 未实现；板端验证仍需等待 MPP H.264 编码通过。

## 6. 涉及名词

- **interrupt_callback**：FFmpeg 的取消回调，网络协议在阻塞等待中周期检查；返回非 0 立即
  以 `AVERROR_EXIT` 结束当前操作。
- **open_timeout / rw_timeout**：TCP 协议的建连超时和读写超时；FFmpeg 默认分别 5 秒与无限。
- **黑洞（blackhole）**：目标不返回任何响应且不回 RST 的网络状态，用于验证超时守卫。
- **RTP over UDP**：媒体走 UDP、控制走 TCP 的 RTSP 常见组合，延迟低但无重传。

## 7. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [RTSP 断连恢复实现与 PC 端到端验证交接](69-RTSP断连恢复实现与PC端到端验证交接.md)
- [RTSP 输出 PC 端到端验证与实现修正交接](68-RTSP输出PC端到端验证与实现修正交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md) 与 [通俗版](07-项目问题汇总-通俗版.md)，P118
