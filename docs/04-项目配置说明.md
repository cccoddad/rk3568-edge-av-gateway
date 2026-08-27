# Mock 与 V4L2 阶段配置说明

配置入口是 `config/mock.json`。加载器会拒绝未知字段和不合理取值，避免字段拼错后悄悄
使用默认值。`schema_version` 当前只能为 `1`。

## runtime

| 字段 | 含义 | 当前约束 |
|---|---|---|
| `mode` | 运行模式 | `mock` 或逐步接硬件时使用的 `hardware` |
| `log_level` | 最低日志级别 | `trace/debug/info/warn/error/fatal` |
| `shutdown_timeout_ms` | 停止操作期限 | 100 至 60000 ms |
| `run_duration_seconds` | 自动停止秒数 | 0 表示直到收到信号 |

命令行 `--duration <秒>` 可覆盖本次运行时长，不修改配置文件。

## video

Mock 视频只生成 `RGB888`。`width`、`height`、`fps` 定义布局和节拍；
`queue_capacity` 是编码队列容量，范围为 1 至 4096。`overflow_policy` 支持
`block_producer`、
`drop_newest`、`drop_oldest`、`keep_latest`，实时视频推荐 `drop_oldest`。

`failure.fail_after_frames=N` 表示从第 N 次读取开始注入失败，连续失败次数由
`fail_for_frames` 指定。`read_delay_ms` 可模拟慢采集。

Linux 构建启用 `RKAV_WITH_V4L2` 后，`backend` 可设为 `v4l2`。第一阶段接受 `MJPEG`
或 `YUYV`，新增字段如下：

| 字段 | 含义 | 当前约束 |
|---|---|---|
| `device` | V4L2 采集节点 | 非空；实测为 `/dev/video9`，重启或拔插后仍须重新枚举 |
| `capture_timeout_ms` | 等待一帧的超时 | 100 至 60000 ms |
| `mmap_buffer_count` | 驱动 MMAP 缓冲数量 | 2 至 32 |

当前硬件阶段配置见 `config/rk3568-v4l2.json`。它只把视频替换为真实 V4L2，音频、推理和
编码仍使用 Mock/Checksum，以便按单变量原则联调。

## audio

Mock 音频只生成 `S16_LE`，支持 `silence` 和 `sine`。每块样本数为：

```text
sample_rate * frame_duration_ms / 1000
```

结果必须是整数。`frame_duration_ms` 最大为 1000；`queue_capacity_ms` 至少容纳一块
音频且最大为 60000。音频队列使用阻塞策略，连续 250 ms 无法入队会作为资源耗尽错误
退出，不能像视频一样静默丢块。

`failure.xrun_every_blocks=N` 每 N 个成功块注入一次可恢复 XRUN；N 必须大于 0。
`disconnect_after_blocks=N` 从第 N 块起持续模拟设备失联，最终由健康检查报告无进展。

## inference

`latency_ms` 模拟一次推理的耗时。推理队列默认容量 1 且采用 `keep_latest`，下游慢时
只保留最新帧。`max_result_age_ms` 定义检测结果的最大有效年龄，过期结果计入指标。

## encoders 与 outputs

当前编码器仅支持 `checksum`，输出包使用明确的
`mock_video_checksum/mock_audio_checksum` codec 标识，不能送给真实播放器。

输出类型支持：

- `null`：丢弃 payload，但校验包结构和每路 DTS 单调性。
- `jsonl`：把包元数据逐行写入 `path`，用于人工检查和自动分析。

每个输出有独立的 `queue_capacity`（1 至 65536）和 `overflow_policy`。`required=true`
的输出发生写入错误会终止应用；非必需输出会隔离并继续运行。`write_delay_ms` 必须小于
关闭超时，它和 `fail_after_packets` 用于可重复故障测试。

## monitoring

`metrics_interval_ms` 控制指标日志周期；`health_interval_ms` 控制工作线程检查周期；
`worker_stall_timeout_ms` 必须大于健康检查周期。运行线程超过该时间没有成功处理数据时，
应用报告具体 worker 并受控停止。
