# 当前开发状态与验收记录

更新日期：2026-08-21

## 1. 已完成范围

当前代码已完成手册 M0 至 M4 的主要实现，可在没有外设和 Rockchip SDK 的环境中构建
Mock 版本。主链路包含双路采集、推理、Checksum 编码、包分发、独立输出线程、结构化
日志、指标和健康检查。

关键正确性约束已经落到代码中：

- 帧、音频块、编码包进入下游前进行布局与时间基校验。
- 所有线程间队列固定容量，并记录高水位、超时和丢弃数量。
- 视频/推理允许按策略丢旧数据；音频默认不静默丢块。
- 统一使用单调微秒时间域，30 FPS deadline 从绝对起点计算，避免累计 sleep 漂移。
- Mock 音频 XRUN 恢复后重试同一逻辑块，不制造隐藏时间戳缺口。
- 可重试设备错误有 100 ms 退避；持续无进展由健康检查升级为致命错误。
- 正常停止按“停止采集、排空处理队列、排空 Sink、关闭后端”的顺序执行。
- 必需 Sink 失败会停止整个管道；慢 Sink 只在自身有界队列内产生丢包。

## 2. 本机验证结果

验证环境：Windows、CMake 4.3.1、Ninja 1.13.2、GCC 15.2.0 MinGW POSIX。

| 检查项 | 结果 |
|---|---|
| 严格告警编译（`-Werror`） | 通过 |
| 默认配置语义校验 | 通过 |
| 单元与集成测试 | 30/30 通过 |
| 正常双流管道 | 通过 |
| 慢推理背压 | 通过，队列容量保持 1 且产生可观测丢帧 |
| 慢 Sink 隔离 | 通过，Sink 队列容量保持 2 且采集持续 |
| 必需 Sink 故障 | 通过，错误上报并进入 failed 状态 |
| 音频 XRUN 恢复 | 通过，PTS 连续且恢复计数增加 |
| 设备持续失联 | 通过，退避重试后由健康检查终止 |

测试命令：

```text
ctest --test-dir C:/Users/CC/.cache/rkav-build/debug --output-on-failure --timeout 15
```

## 3. RK3568 Buildroot 实机进展

- 已识别板端为 aarch64、Buildroot 2018.02、Linux 4.19，而不是 Ubuntu/systemd。
- 已在 Ubuntu 虚拟机生成 ARM64 静态 `rkav-gateway` 和 `rkav_tests`。
- 已通过串口配置电脑直连网络，并使用 SSH/SCP 部署程序。
- 板端配置校验通过；10 秒 Mock 双流运行正常结束，801 个包全部消费，错误和丢弃均为 0。
- 本次留存输出尚未包含板端 `rkav_tests` 的 30 项最终汇总，因此不能声明板端测试已通过。

详细证据见
[Buildroot 交叉编译与 RK3568 上板阶段总结](../09-buildroot-cross-compile-and-board-bringup-summary.md)。

## 4. 尚未完成或尚未实机验证

- 已记录架构、系统和内核；CPU、内存、温度和磁盘的完整基线仍需留档。
- 尚未执行 Linux ASan/UBSan；Windows MinGW 仅完成普通 Debug 测试。
- Windows PATH 中没有原生 clang-format/clang-tidy；已用临时 clang-format 工具统一格式，
  GitHub Actions 的格式检查、clang-tidy、Debug、30 项测试和 ASan/UBSan 已全部通过。
- 尚未执行 30 分钟和 2 小时 RK3568 长稳测试。
- systemd 文件已提供，但当前 Buildroot 使用 SysV init；需另做 BusyBox 兼容启动脚本。
- SIGINT/SIGTERM 处理代码已实现，仍需在 Linux 进程级集成测试中验证。
- 进程 RSS/CPU 不在程序内部指标中，长稳脚本从操作系统侧采集。
- V4L2、ALSA、RKNN、RGA、MPP、H.264、AAC、MP4、RTSP 都未实现。
- 当前推理结果完成了时效统计和帧绑定，但尚无真实 OSD 像素叠加阶段。

因此当前版本是可测试、可演进的 Mock 工程基线，不是具备真实音视频输入输出能力的
最终产品。

## 5. 下一阶段顺序

1. 冷启动验证固定 IP，并定位 `reboot` 停在 `usbdevice stop` 的问题。
2. 在板端运行 `rkav_tests`，保存退出码和 30 项汇总。
3. 完成 SIGINT/SIGTERM、30 分钟长稳及 RSS、CPU、温度和队列指标留档。
4. 获取当前 BSP 对应 SDK/sysroot，再新增 V4L2 后端，不改 `IVideoCapture` 和 Application 主链路。
