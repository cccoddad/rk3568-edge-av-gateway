# RTSP 网络输出模块实现与 systemd 服务优化

更新日期：2026-09-12

## 1. 当前结论

本轮已完成 **RTSP 网络输出模块**的代码实现和 **systemd 服务文件优化**。Windows Debug 环境
54 项测试全部通过（1 项因无真实摄像头样本跳过），新增 3 项 RTSP 配置测试和 5 项 RTSP Sink
工厂测试。

RTSP 模块基于 FFmpeg `rtsp` muxer，支持 TCP 传输，可将 H.264/AAC 编码包通过 RTSP 协议
推送到网络。该模块尚未在板端进行实际网络流测试。

## 2. 实现范围

### 2.1 RTSP 输出模块

| 文件 | 说明 |
|---|---|
| `include/rkav/output/ffmpeg_rtsp_sink.h` | RTSP Sink 公共头文件，继承 `IPacketSink` |
| `src/output/ffmpeg/ffmpeg_rtsp_sink.cpp` | 基于 FFmpeg libavformat RTSP muxer 的实现 |
| `config/mock-ffmpeg-rtsp.json` | RTSP 输出测试配置（Mock 源 + FFmpeg H.264/AAC） |
| `tests/unit/rtsp_sink_test.cpp` | RTSP Sink 工厂和配置校验测试 |

**关键设计决策**：
- 使用 `avformat_alloc_output_context2` 自动识别 `rtsp://` URL 并选择 RTSP muxer
- 默认 TCP 传输（`rtsp_transport=tcp`），比 UDP 更可靠，适合嵌入式场景
- 音频流可选：仅 H.264 视频即可建立 RTSP 会话
- 遵循现有 Sink 模式：`Open → Write → Flush → Close`，线程安全

### 2.2 配置校验

在 `config.cpp` 中新增 RTSP 输出校验：
- URL 必须以 `rtsp://` 开头
- 视频编码器必须是 `ffmpeg` 或 `mpp`（真实 H.264 编码器）
- FFmpeg 未编译时返回 `"RTSP output is not compiled in"`

### 2.3 systemd 服务优化

| 改进 | 说明 |
|---|---|
| USB 设备就绪依赖 | 增加 `After=systemd-udev-settle.service` 等待 USB 设备枚举 |
| 设备访问控制 | `DeviceAllow` 限制 `/dev/video*`、`/dev/snd/*`、`/dev/dri/*` |
| 用户组权限 | `SupplementaryGroups=video audio` 允许访问设备节点 |
| 重启频率限制 | 5 分钟内最多 5 次重启，防止故障循环 |
| 资源限制 | `MemoryMax=512M`、`CPUQuota=80%` 防止资源耗尽 |
| 日志集成 | `SyslogIdentifier=rkav-gateway` 便于 `journalctl` 过滤 |
| 默认配置 | 改为 `rk3568-rknn-mjpeg-alsa.json`（三硬件配置） |

## 3. 测试结果

```
54/54 tests passed (1 skipped)
```

新增测试：
- `ConfigTest.RtspOutputConfigurationRequiresCompiledFeature` — FFmpeg 编译依赖
- `ConfigTest.RtspOutputRequiresRtspUrl` — URL 协议校验
- `ConfigTest.RtspOutputRequiresRealH264Encoder` — 编码器类型校验
- `RtspSinkFactoryTest.*` — 工厂创建和配置校验（5 项）

## 4. 未完成项

- RTSP 模块尚未在 RK3568 板端进行实际网络流测试
- RTSP 客户端连接管理和断连恢复尚未实现
- 尚未配置 Linux 防火墙规则开放 RTSP 端口（默认 8554）
- systemd 服务尚未在板端进行 2/12 小时长稳验证

## 5. 下一步

1. 板端部署 RTSP 配置，使用 VLC 或 ffplay 拉流验证
2. 实现 RTSP 客户端连接管理和断连恢复
3. 配置 Linux 防火墙规则
4. 完成 systemd 服务化验证

## 6. 专有词

- **RTSP**：Real Time Streaming Protocol，实时流传输协议，用于在网络上推送/拉取音视频流。
- **Muxer**：复用器，将多路音视频流交织封装为单一输出流的组件。
- **TCP 传输**：RTSP over TCP，比 UDP 更可靠，避免丢包导致花屏。
- **systemd**：Linux 系统和服务管理器，负责进程托管、自动重启和资源限制。

## 7. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [FFmpeg 软件 MP4 基线验收与交接](25-FFmpeg软件MP4基线验收与交接.md)
- [MPP/RGA 首次板端 ABI 短测失败与下一步交接](30-MPP-RGA首次板端ABI短测失败与下一步交接.md)
