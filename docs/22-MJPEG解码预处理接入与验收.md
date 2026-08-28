# MJPEG 解码预处理接入与验收

更新日期：2026-08-28

## 1. 阶段结论

主程序已经具备从 V4L2 CPU MJPEG 帧到紧密 RGB888 帧的预处理能力。实现使用固定版本
libjpeg-turbo 3.1.4.1，只在 `max_fps` 推理抽帧之后解码，因此 30 FPS 原始视频分支不承担
无意义的全帧 RGB 展开成本。PC 侧功能、真实摄像头样本和板端 10 秒实时 MJPEG + RKNN
均已通过；60 秒稳定性、真实 MJPEG + ALSA + RKNN 10 秒联合验收和 30 分钟三硬件联合长稳
也已通过。30 分钟原控制器存在指标语义错误，项目保留原始失败记录并依据未改动证据完成了
修正规则后的重验收。

## 2. 依赖与构建边界

- 官方源码版本：libjpeg-turbo 3.1.4.1。
- 源码包 SHA-256：
  `ecae8008e2cc9ade2f2c1bb9d5e6d4fb73e7c433866a056bd82980741571a022`。
- 使用 TurboJPEG 内存 API，不依赖板端未知版本的 `libjpeg.so`。
- 交叉构建静态链接 `libturbojpeg.a`，关闭 SIMD 以避免交叉汇编工具差异。
- 构建脚本仍要求最终 ELF 只动态依赖板端已有的 glibc、libm 和 `librknnrt.so`，并拒绝
  高于 GLIBC 2.35 的符号。

## 3. 代码路径

`IVideoDecoder` 定义稳定生命周期；`JpegVideoDecoder` 负责 TurboJPEG 句柄和解码。Application
在 V4L2 实际协商为 MJPEG 且推理后端为 RKNN 时打开解码器，推理线程按以下顺序运行：

```text
V4L2 MJPEG 30 FPS
-> inference max_fps sampling (5 FPS)
-> bounded keep_latest queue (capacity 1)
-> JPEG header validation and RGB888 decode
-> RKNN resize / inference / YOLOv5 postprocess
```

输出帧保持采集帧的 sequence 和单调 PTS，宽高以 JPEG 头为准。单帧坏 JPEG 返回可恢复
codec 错误并被跳过，不终止整条实时流；尺寸越界、整数溢出和分配失败均有明确错误。

## 4. PC 验收证据

- Windows Debug：43/43 项测试通过。
- Windows Release：43/43 项测试通过。
- 固定 3x2 JPEG：布局、元数据和确定性解码通过。
- 损坏 JPEG：返回可恢复错误，随后同一解码器可恢复处理正常帧。
- UGREEN 摄像头真实样本：58,120 字节，解码为 1280x720、stride 3840、RGB payload
  2,764,800 字节；Debug 与 Release 均通过。

## 5. 板端 10 秒验收结果

2026-08-27 在 RK3568 Buildroot、RKNN Runtime 1.4.0、NPU 驱动 0.8.2 上完成：

| 指标 | 结果 |
|---|---:|
| 实际协商视频 | `/dev/video9`，1280x720 MJPEG，目标 30 FPS |
| 运行时间 | 10 秒配置，约 10.39 秒完整生命周期 |
| 视频采集/视频包 | 269 / 269 |
| JPEG 解码样本 | 49 |
| RKNN 请求/结果 | 49 / 49 |
| NPU 中断增量 | 49（61 -> 110） |
| JPEG 解码延迟 p50/p95/p99 | 32.471 / 60.856 / 63.042 ms |
| RKNN 推理延迟 p50/p95/p99 | 144.332 / 154.692 / 159.296 ms |
| 音频块/总消费包 | 511 / 780 |
| 错误/恢复/队列丢弃 | 0 / 0 / 0 |
| 过期检测结果 | 4 |
| 温度 | 46.666 -> 49.444 摄氏度 |
| 新增 NPU/IOMMU 错误 | 0 |

所有队列停止后均为 `closed=true`、`size=0`，推理队列高水位为 1，证明容量 1 的
`keep_latest` 策略没有形成积压。4 次过期检测只占 269 个视频帧的约 1.5%，不是推理失败；
它表示编码线程在少数时刻看到的最新检测结果年龄超过配置的 400 ms，应在 60 秒测试中继续
观察是否线性增长。

## 6. 板端 60 秒验收结果

同一程序和配置扩展运行 61.170 秒：

| 指标 | 结果 |
|---|---:|
| 视频采集/视频包 | 1,660 / 1,660 |
| 实际视频速率 | 27.137 FPS |
| JPEG 解码样本 | 291 |
| RKNN 请求/结果 | 291 / 291 |
| NPU 中断增量 | 291（110 -> 401） |
| JPEG 解码延迟 p50/p95/p99 | 32.672 / 60.810 / 67.374 ms |
| RKNN 推理延迟 p50/p95/p99 | 141.814 / 159.534 / 165.349 ms |
| Mock 音频块/总消费包 | 3,010 / 4,670 |
| 错误/恢复/队列丢弃 | 0 / 0 / 0 |
| 过期检测结果 | 28，占视频帧约 1.69% |
| RSS 采样范围 | 启动时 4,060 KiB；工作态约 45,128–57,796 KiB |
| 温度 | 46.111 -> 50.000 摄氏度，采样峰值 51.875 摄氏度 |
| 新增 NPU/IOMMU 错误 | 0 |

10 秒与 60 秒的解码、推理延迟分位数接近，过期结果比例也接近，未观察到随时间恶化。
RSS 末段升至约 56.4 MiB，可能包含 JPEG/RGB 大缓冲释放后的分配器缓存；当时 60 秒不能单独
证明内存泄漏，后续 30 分钟长稳已确认工作态 RSS 前后均为 57,652 KiB、增长为 0。所有队列
零丢弃、停止后 size 为 0。

## 7. 真实 ALSA + MJPEG + RKNN 10 秒联合验收

第一次联合运行中，ALSA 在 `Open()` 内执行 `SNDRV_PCM_IOCTL_START`，随后 Application 继续
初始化 RKNN 并创建工作线程。音频硬件此时已经写入数据，但程序约 199 ms 后才开始读取；
当前 ALSA 缓冲区只有约 4 个 20 ms period，即约 80 ms，因而启动阶段确定性出现一次
`EPIPE`/XRUN。它不是 USB 麦克风失效，而是后端生命周期顺序错误。

修复后，`Open()` 只完成参数协商和 `PREPARE`，首个音频工作线程进入 `Read()` 时才执行
`START`，并在同一时刻建立首个单调 PTS。`Recover()` 和 `Close()` 同步维护 `started_` 状态。
修复版 AArch64 ELF 的 SHA-256 为
`9d29d4327e731ecff3b5835c14f5fb70c9e58d556fcd0eb85298005601da6623`，最高 GLIBC 需求仍为
2.34。

2026-08-27 在真实 `/dev/video9`、`hw:2,0` 和 RKNN NPU 上重新运行 10 秒：

| 指标 | 结果 |
|---|---:|
| 完整测量时长 | 11.020 秒 |
| 视频采集/视频包 | 276 / 276，约 25.045 FPS（含启停边界） |
| 音频采集/音频包 | 500 / 500 |
| JPEG 解码/RKNN 请求/RKNN 结果 | 49 / 49 / 49 |
| NPU 中断增量 | 49（450 -> 499） |
| 路由/消费包 | 776 / 776，等于 276 + 500 |
| 错误/恢复/工作线程错误 | 0 / 0 / 0 |
| 队列丢弃/停止后残留 | 0 / 0 |
| JPEG 解码延迟 p50/p95/p99 | 33.547 / 57.900 / 65.900 ms |
| RKNN 推理延迟 p50/p95/p99 | 144.395 / 161.985 / 166.435 ms |
| 温度 | 46.111 -> 48.333 摄氏度 |
| 新增 USB/ALSA/NPU/IOMMU 错误 | 0 |

自动验收标记为 `alsa_start_fix_10s=passed`。49 次解码、49 次推理结果和 49 次 NPU 中断
一一对应，500 个 20 ms 音频块也正好覆盖 10 秒。与修复前唯一一次启动 XRUN 相比，修复后
`recoveries_total=0` 且没有 `worker_error`，因此已经证明启动顺序修复生效。

## 8. 30 分钟三硬件联合长稳结果

后台重试结果目录为：

```text
/userdata/rkav/gateway-alsa-start-fix-20260827/real-mjpeg-alsa-rknn-30m-retry-1
```

| 指标 | 结果 |
|---|---:|
| 配置时长/资源样本 | 1,800 秒 / 179 |
| 视频采集/视频包 | 50,237 / 50,237，约 27.91 FPS |
| 音频采集/音频包 | 89,983 / 89,983，按 20 ms 计约 1,799.66 秒 |
| RKNN 请求/结果 | 8,714 / 8,714 |
| NPU 中断增量 | 8,714 |
| 预期/路由/消费包 | 140,220 / 140,220 / 140,220 |
| 错误/恢复/工作线程错误 | 0 / 0 / 0 |
| 队列丢弃/停止后残留 | 0 / 0 |
| 工作态 RSS 早期/晚期/增长 | 57,652 / 57,652 / 0 KiB |
| 线程最小/最大 | 9 / 9 |
| 最高温度 | 57.222 摄氏度 |
| 过期检测结果 | 626，约占视频帧 1.25% |
| JPEG 解码延迟 p50/p95/p99 | 35.704 / 59.208 / 64.494 ms |
| RKNN 推理延迟 p50/p95/p99 | 139.723 / 156.241 / 161.486 ms |

网关退出码为 0，温度保护未触发。`dmesg` 增量只有已知 UAC `cannot get freq` warning，未出现
新的 USB 断连、XRUN 或 NPU/IOMMU fault、timeout、panic、error。

原控制器最终写出 `status=failed:1`，原因不是业务或硬件失败，而是以下两条规则错误地把
`video_decode.samples` 当成累计解码计数：

```sh
test "$decode_samples" -ge 8500
test "$decode_samples" -eq "$requests"
```

延迟指标只保留一个有上限的滑动窗口；达到 4,096 个样本后会定期删除最旧四分之一，因此最终
3,594 表示窗口内还保存的延迟样本，不表示全程只解码了 3,594 次。累计工作量应检查
`inference_requests_total >= 8500`，并继续要求请求数、结果数和 NPU 中断增量相等。未来控制器
应使用：

```sh
test "$requests" -ge 8500
test "$decode_samples" -gt 0
test "$decode_samples" -le 4096
test "$requests" -eq "$results"
test "$npu_delta" -eq "$results"
```

项目没有覆盖原始 `status` 和日志，而是新增 `original-evidence.sha256` 与
`corrected-validation.log`。五份原始证据均通过 SHA-256 复核，修正规则后的结论为
`three_hardware_soak_30m=passed_after_validation_rule_correction`。后续真实 RKNN 链路 USB
断连与重插重启也已通过，详见
[USB 断连与重插恢复验收](24-USB断连与重插恢复验收.md)。这些通过不代表 H.264/AAC、MP4、
OSD、MPP/RGA 或 RTSP 已完成。
