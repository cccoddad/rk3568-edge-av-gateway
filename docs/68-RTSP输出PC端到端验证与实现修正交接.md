# RTSP 输出 PC 端到端验证与实现修正交接

更新日期：2026-09-12

## 1. 当前结论

RTSP 输出此前**从未真正联网成功**，本轮在无硬件条件下定位并修复了两个阻断级问题，且在
Ubuntu 24.04 + FFmpeg 6.1.1 环境完成 **push 模式端到端验证**：

1. **`avio_open` 误用（原实现必然失败）**：FFmpeg 的 RTSP 是 muxer/demuxer 而不是 protocol，
   `rtsp` muxer 标记 `AVFMT_NOFILE`，网络连接由 `avformat_write_header` 内部建立。旧实现对
   URL 调用 `avio_open`，协议表中不存在 `rtsp` 协议，该路径必然报错。
2. **`rtsp_mode=listen` 假设错误（本轮验证否决）**：FFmpeg 的 RTSP **muxer 不支持监听端口**；
   `listen` 选项只存在于 demuxer（共享选项表中标记为 `DEC`）。按错误假设添加的 listen 配置
   表现为网关尝试连接 `0.0.0.0:8554` 被拒绝，E2E 首次运行即失败并留下证据。

修正后验证结果：

- 接收端用 `ffmpeg -rtsp_flags listen`（demuxer 监听模式）当服务器，网关以 push（ANNOUNCE）
  模式推流成功；
- 捕获 MP4：H.264 Constrained Baseline 640x360 + AAC LC 48 kHz 双声道，时长 5.021 秒；
- 接收端 5 秒后断开，RTSP sink 被隔离（日志一次写入失败），网关继续跑满 30 秒配置时长，
  退出码 0；
- Ubuntu 原生构建（FFmpeg ON）**59/59 测试通过**，其中 RTSP sink 测试为首次真正执行。

同时修复 `deploy/rkav-gateway.service` 的真实配置错误：`StartLimitBurst` /
`StartLimitIntervalSec` 从 `[Service]` 移动到 `[Unit]`，`systemd-analyze verify` 退出码 0。

## 2. 问题与证据链

### 2.1 `avio_open` 误用

- `rtsp` muxer 在 `libavformat/rtspenc.c` 中注册为 `AVFMT_NOFILE | AVFMT_GLOBALHEADER`，
  `avformat_alloc_output_context2` 不会创建 `pb`。
- 旧代码在 `pb == nullptr` 时调用 `avio_open(&pb, "rtsp://...")`；FFmpeg 协议层没有名为
  `rtsp` 的 URLProtocol（RTSP 由 muxer 处理），该调用在任何环境都会失败。
- 该缺陷此前无法在 Windows/CI 暴露：FFmpeg 后端在 Windows 默认不编译，相关测试从未运行。

### 2.2 `rtsp_mode=listen` 假设被否决

只读证据（PC，FFmpeg 6.1.1）：

```text
ffmpeg -h muxer=rtsp
RTSP muxer AVOptions:
  -rtpflags / -rtsp_transport / -min_port / -max_port / -buffer_size / -pkt_size
# 没有 rtsp_flags，也没有 listen
```

源码比对（`libavformat/rtspenc.c` 的 `n4.4`、`n6.1`、`n7.1` 三个版本）：

- muxer 类直接复用 `libavformat/rtsp.c` 的 `ff_rtsp_options`；
- 其中 `listen` 常量标记为 `DEC`（仅 demuxer）；muxer 输出侧不存在监听路径。

实验证据（E2E 首次运行，失败目录 `D:\share\rtsp-e2e-20260912-154116`）：

```text
gateway_alive_after_3s=no
[tcp] Connection to tcp://0.0.0.0:8554?timeout=0 failed: Connection refused
ffmpeg_rtsp_sink.write_header: cannot write RTSP stream header: Connection refused
```

即网关尝试作为客户端连接而不是监听，与源码结论一致。该失败目录保留为 listen 假设被否决的
原始证据（含 `used-config.json`、网关日志和 SHA256SUMS）。

### 2.3 其他修正

- `avformat_alloc_output_context2` 显式传入 `"rtsp"` muxer 名称，不再依赖 URL 自动识别。
- `Open` 增加 URL 必须 `starts_with("rtsp://")` 的前置校验。
- `Release` 仅对非 `AVFMT_NOFILE` 的 muxer 调用 `avio_closep`。
- 删除对 muxer 无意义的 `gop_size` 选项（GOP 属于编码器配置，不属于 muxer）。
- 保留 `rtsp_transport=tcp`：嵌入式网络下 TCP 交织比 UDP 更可靠。

## 3. 代码与配置修改清单

| 文件 | 修改 |
|---|---|
| `src/output/ffmpeg/ffmpeg_rtsp_sink.cpp` | NOFILE 语义修正、显式 rtsp muxer、URL 校验、移除 `avio_open` 与 `gop_size` |
| `tests/unit/rtsp_sink_test.cpp` | 重写为工厂测试 + Open 级校验（空 URL、非 rtsp 协议、缺少 H.264 流） |
| `config/mock-ffmpeg-rtsp.json` | 路径改为 `rtsp://127.0.0.1:8554/live`（push 目标服务器语义） |
| `deploy/rkav-gateway.service` | `StartLimitBurst` / `StartLimitIntervalSec` 移入 `[Unit]` |
| `include/rkav/config/config.h` | `OutputConfig::type` 注释补全 `h264`、`rtsp` |

曾短暂引入的 `rtsp_mode` 配置项、校验规则和 3 项配置测试已全部回退，避免保留无效字段。

## 4. PC 端到端验证证据

| 项目 | 值 |
|---|---|
| 环境 | VMware Ubuntu 24.04.3 虚拟机，FFmpeg/ffprobe 6.1.1，libx264 |
| 源码归档 | `rkav-rtsp-push-source-20260912-1547.tar.gz`，SHA-256 `e249a91a...623c9` |
| 构建 | 原生 x86_64 Debug，`RKAV_WITH_FFMPEG=ON`，44/44 目标通过 |
| 测试 | `ctest` 59/59 通过（含 5 项 RTSP sink 测试） |
| 接收命令 | `ffmpeg -rtsp_flags listen -rtsp_transport tcp -i rtsp://127.0.0.1:8554/live -frames:v 150 -c copy` |
| 捕获文件 | `rtsp-capture.mp4` 131703 字节，SHA-256 `4b3f36e3...99490` |
| 流信息 | H.264 Constrained Baseline 640x360 + AAC LC 48000 Hz 双声道，时长 5.020933 秒 |
| 网关行为 | 客户端断开后 sink 隔离（errors_total=2），跑满 30 秒，退出码 0，`reason=run_duration_elapsed` |
| 服务文件校验 | `systemd-analyze verify` 退出码 0、无输出 |
| 证据目录 | 虚拟机共享目录 `rtsp-push-e2e-20260912-154715`（Windows 侧 `D:\share\` 同名目录） |

测试用配置由 `config/mock-ffmpeg-rtsp.json` 生成：时长 30 秒、`required=false`（验证断开后
隔离行为）；仓库内配置保持 `required=true`（生产语义：推流目标不可丢失）。

已知无害警告：接收端 mp4 muxer 打印一次 `Timestamps are unset in a packet for stream 0`，
产物时长、音视频轨和 PTS 均正常，不阻塞验收。

## 5. 边界与未完成项

- PC 验证使用 FFmpeg **软件编码**（libx264/aac）+ 本机回环 + TCP，不代表板端 MPP 编码、
  跨网段、UDP 或丢包场景；板端 RTSP 仍按固定顺序在 MPP H.264 通过后接入。
- RTSP 客户端断连后的**进程内自动重连尚未实现**；当前语义是可选 sink 隔离或必需 sink
  停止管道，重启由人工或未来的 systemd/重连逻辑承担。
- 未验证 RTSP over UDP、鉴权（RTSP digest）、TLS 和防火墙策略。
- `systemd-analyze verify` 只证明 unit 语法正确，板端 2/12 小时托管长稳仍未进行。

## 6. 涉及名词

- **AVFMT_NOFILE**：muxer 自管网络/内存输出、不通过 `AVIOContext` 打开 URL 的标志；RTSP、
  RTP、FIFO 等属于此类。对这类 muxer 调用 `avio_open` 属于接口误用。
- **AVOption 作用域（E/D）**：FFmpeg 选项表用 `E`（编码/封装）和 `D`（解码/解封）标记可用侧；
  `listen` 只标记为 `D`，因此只能用于输入（拉流/收流）侧。
- **ANNOUNCE/RECORD 与 PLAY**：RTSP 推送（muxer）先向服务器 ANNOUNCE 描述媒体再 RECORD；
  拉流（demuxer）则用 DESCRIBE/PLAY。`-rtsp_flags listen` 的 demuxer 模式用于**接收**其他
  推流端，两者组合正好构成 PC 端到端测试的推/收两端。
- **StartLimitIntervalSec / StartLimitBurst**：systemd 重启频率限制，属于 `[Unit]` 段指令，
  放在 `[Service]` 段会被忽略。

## 7. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [RTSP 网络输出模块实现与 systemd 服务优化](65-RTSP网络输出模块实现与systemd服务优化.md)
- [FFmpeg 软件 MP4 基线验收与交接](25-FFmpeg软件MP4基线验收与交接.md)
- [CPU OSD 软件 MP4 验收与交接](26-CPU-OSD软件MP4验收与交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md) 与 [通俗版](07-项目问题汇总-通俗版.md)，P116
