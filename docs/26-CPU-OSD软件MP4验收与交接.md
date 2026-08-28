# CPU OSD 软件 MP4 验收与交接

更新日期：2026-08-28

## 1. 文档用途

本文记录在 FFmpeg H.264/AAC MP4 软件基线之上完成的 CPU 检测框和文字 OSD。前一阶段的
编码、封装和时间戳证据见
[FFmpeg 软件 MP4 基线验收与交接](25-FFmpeg软件MP4基线验收与交接.md)。

本阶段是 CPU 像素处理基线，后续 RGA 实现必须复用 `DetectionBatch`、来源帧绑定和 Overlay
接口，而不是重新解释 RKNN tensor 或放宽时间关联规则。

## 2. 一句话结论

CPU OSD 已通过 Ubuntu Mock + FFmpeg 软件 MP4 验收。5 秒运行产生 151 个视频帧、151 次
推理请求和 151 次推理结果，`overlay_applied_total=151`、`overlay_skipped_total=0`；每个送入
本轮 Mock 推理的帧均在等待上限内取得相同 `sequence` 和 PTS 的检测结果后完成画框与文字叠加。
生成的 MP4 是 H.264/AAC，5.040 秒，音视频起始 PTS 均为 0，PTS/DTS 单调，所有队列零丢弃和
零残留。

## 3. 实现范围

- 新增 `OverlayConfig`：`enabled`、`backend=cpu`、`line_width`、`draw_labels` 和
  `wait_for_result_ms`；默认关闭，既有板端配置不会被悄悄改变。
- 新增 `IOverlay` 与 `CpuOverlay`。CPU 后端只接受 RGB888/BGR888，绘制裁剪后的检测框和
  `C<类别 ID> <置信度>%` 文字标签；输入为 MJPEG 时，编码支路先使用已有 JPEG 解码器转为 RGB。
- OSD 复制源帧 Buffer 后再写像素，推理线程继续读取原始共享帧，不会因画框污染模型输入。
- 采集线程记录 `inference_submitted`。编码线程只对实际送入推理的帧等待最多
  `wait_for_result_ms`，并同时要求 `frame_sequence` 与 `source_pts_us` 精确相等；结果过期、
  迟到、属于旧帧或属于未来帧时均不绘制。
- 新增 `overlay_applied_total`、`overlay_skipped_total` 和 `overlay` 延迟分位数。软件 MP4
  验收脚本把 `overlay_applied_total > 0` 作为硬性通过条件。

## 4. 本机测试

Windows Debug 通过 49/49 测试，真实相机 JPEG 样本测试因未提供样本而按设计跳过。新增测试覆盖：

- RGB 画框和文字后源 Buffer 不变；
- BGR 通道顺序、越界框裁剪和压缩输入拒绝；
- OSD JSON 严格配置与等待上限；
- 多线程 Application 中仅对精确来源帧应用 OSD。

## 5. Ubuntu 软件 MP4 证据

```text
Archive: /mnt/hgfs/share/rkav-osd-mp4-source-20260828-145613.tar.gz
Archive SHA-256: 39e33e83ad0451748d7c160bc3fbd04a478ee4779fa2980b4159b9f6b2bb033e
Work: /tmp/rkav-osd-mp4-20260828-145613
Result: /tmp/rkav-osd-mp4-20260828-145613/out/ffmpeg-baseline-20260828-150727-6029
```

源码包哈希、解压后的 Shell 语法和控制器均通过，`baseline_exit_code=0`。结果目录保存 MP4、
配置、网关日志、`ffprobe-validation.log`、CTest 输出、依赖版本和 `SHA256SUMS`；网关、配置、
MP4、日志和验收日志五项哈希复核均为成功。

| 项目 | 结果 |
|---|---:|
| 视频/推理请求/推理结果 | 151 / 151 / 151 |
| 实际 OSD 叠加/跳过 | 151 / 0 |
| 视频编码 | H.264，320x180 |
| 音频编码 | AAC，48 kHz 双声道 |
| MP4 时长 | 5.040000 秒（目标 5 秒，容差 0.5 秒） |
| 视频/音频起始 PTS | 0.000000 / 0.000000 秒 |
| PTS/DTS | 两个流均自动检查通过 |
| Overlay p50/p95/p99 | 39 / 145 / 260 微秒 |
| 路由/MP4 Sink 消费 | 389 / 389 |
| 错误/恢复/队列丢弃/残留 | 0 / 0 / 0 / 0 |

`ffprobe` 验证结构和时间轴；CPU 像素单元测试与 `overlay_applied_total` 共同证明检测框和文字
实际经过叠加、编码和封装。没有把这次 Mock 软件验收表述为人工播放器目视验收或板端实机结果。

## 6. 当前精确停止位置与边界

当前顺序进入 RGA 优化和 MPP H.264 接入。CPU OSD 已提供正确性和接口基线，RGA 只能替换像素
实现，仍需保留精确来源帧约束、有限等待、错误传播和指标。

尚未完成：RGA、MPP H.264、RK3568 实机 OSD/MP4、RTSP 与网络恢复、systemd、SIGTERM 媒体
收尾、日志轮转和 2 小时/12 小时长稳。实际板端 30 FPS 视频与 5 FPS RKNN 推理并不保证每帧都
有检测结果；当前策略宁可保持无框原画面，也绝不把旧框画到新帧。

## 7. 专有词

- **OSD（On-Screen Display）**：直接在视频像素上画框、文字等叠加内容，再交给编码器。
- **精确来源帧绑定**：检测结果的采集序号和 PTS 同时匹配待编码帧，才允许画框。
- **有界等待**：只等待预设最长时间，到时立即继续，不允许线程无限阻塞。
- **RGA**：Rockchip 图形加速器，后续可替换 CPU 的缩放、色彩转换和叠加计算。
- **MPP**：Rockchip Media Process Platform，后续用于 H.264 硬件编码。

## 8. 相关文档

- [整体代码架构](01-项目总体代码架构.md)
- [项目当前开发状态](19-项目当前开发状态.md)
- [FFmpeg 软件 MP4 基线验收与交接](25-FFmpeg软件MP4基线验收与交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md)
- [项目问题汇总：通俗版](07-项目问题汇总-通俗版.md)
