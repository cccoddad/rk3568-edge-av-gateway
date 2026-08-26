# RK3568 实时音视频边缘分析网关

这是按《开发全流程手册》实现的第一版正式工程。Mock 后端用于可重复回归，Linux 上已经
加入并实机验证真实 V4L2 摄像头和 ALSA 麦克风后端；推理、编码与输出仍保留 Mock/Checksum
实现。硬件替换不修改公共数据契约和 Application 主流程。

## 当前完成度

- M0 工程基线：CMake targets、presets、严格编译告警、格式和静态检查配置。
- M1 公共核心：Error/Result、Buffer、Frame/Packet、单调时钟、时间戳换算、有界队列。
- M2 Mock 数据：RGB 测试图、连续正弦波、可配置视频失败、XRUN 和设备失联。
- M3 多线程管道：Mock 推理、Checksum Encoder、PacketRouter、Null/JSONL Sink。
- M4 工程能力：强类型配置、结构化日志、指标、工作线程健康检查和故障退出。
- 自动测试：34 项单元与集成测试，覆盖正常、慢推理、慢 Sink、故障场景和硬件配置契约。

ARM64 静态 Mock 已在 RK3568 Buildroot 完成 33 项当前 PC 回归基线、板端 30 项旧基线、
信号退出、30 分钟和 2 小时长稳。绿联 2K 摄像头已完成 UVC/UAC 枚举、720p MJPEG 样本、
48 kHz 双声道 WAV 和 300 帧持续采集验收。第一版 V4L2 MMAP 后端已完成 ARM64 交叉编译，
并在板端通过 60 秒 Application 联调：1,741 帧、约 29.0 FPS、零错误和零队列丢弃。ALSA
内核 UAPI 后端也已完成 ARM64 交叉编译和板端双路联调：10 秒采集 500 个音频块和 285 帧
视频；30 分钟采集 90,000 个音频块和 52,340 帧视频，RSS 稳定为 9,728 KiB，错误、恢复和
队列丢弃均为 0，SIGINT/SIGTERM 也能排空队列并正常退出。RKNN、RGA、MPP 和实际 RTSP/MP4
输出仍未接入，不能把当前 Checksum packet 当作 H.264/AAC 成品。

## 数据流

```text
Mock/V4L2VideoCapture -> video queue -> ChecksumVideoEncoder --+
                      -> inference queue -> MockInference       |
                                                               +-> PacketRouter -> sinks
Mock/AlsaAudioCapture  -> audio queue -> ChecksumAudioEncoder    +
```

每个跨线程队列都有固定容量。视频与推理队列优先保留新数据；音频队列阻塞生产者并在
长期阻塞时明确报错。每个输出端有独立队列，因此慢输出不会阻塞其他输出或采集线程。

## 快速开始

### Windows / PowerShell

仓库当前目录名含非 ASCII 字符，部分 Windows Ninja 版本无法直接处理。脚本会在
`%LOCALAPPDATA%/rkav-gateway` 下建立短路径 Junction，并在短路径构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_windows.ps1
```

脚本会依次配置、编译、运行全部测试并校验默认配置。要求命令行可找到 CMake 3.20+、
Ninja、支持 C++20 的 GCC/Clang/MSVC，以及 Git。首次构建会下载锁定版本的
nlohmann/json 和 GoogleTest。

### Ubuntu / RK3568 Debian 系统

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git
sh ./tools/build_and_test.sh
```

也可手动执行：

```bash
cmake --preset debug
cmake --build --preset debug -j4
ctest --preset debug
./build/debug/rkav-gateway --validate-config --config config/mock.json
./build/debug/rkav-gateway --config config/mock.json
```

默认配置运行 10 秒后正常退出。持续运行到收到 `SIGINT`/`SIGTERM`：

```bash
./build/debug/rkav-gateway --config config/mock.json --duration 0
```

## 常用命令

```bash
# 查看帮助和版本
./build/debug/rkav-gateway --help
./build/debug/rkav-gateway --version

# 仅校验配置，不创建线程
./build/debug/rkav-gateway --validate-config --config config/mock.json

# 本次运行覆盖为 60 秒，不修改 JSON
./build/debug/rkav-gateway --config config/mock.json --duration 60

# Linux ASan + UBSan
cmake --preset asan
cmake --build --preset asan -j4
ctest --preset asan

# 30 分钟长稳并采集资源数据
sh ./tools/soak_test.sh 1800
```

RK3568 M5 板端基线、Sanitizer、SIGTERM、长稳和 systemd 的完整验收步骤见
[M5 板端验收](docs/08-m5-board-validation.md)。

当前粤嵌 Buildroot 的交叉编译、直连网络、SSH 部署、实测指标和下一步见
[Buildroot 交叉编译与 RK3568 上板阶段总结](docs/09-buildroot-cross-compile-and-board-bringup-summary.md)。

真实绿联摄像头和麦克风的 VID/PID、格式、样本与持续采集证据见
[UGREEN 2K USB 音视频设备验收](docs/11-ugreen-camera-and-microphone-validation.md)。

## 目录说明

| 路径 | 职责 |
|---|---|
| `include/rkav/common` | 公共错误、内存、时间和数据契约 |
| `include/rkav/capture` | 视频/音频采集接口及 Mock 声明 |
| `include/rkav/vision` | 推理接口和图像坐标变换 |
| `include/rkav/media` | 编码接口和 Checksum 测试编码器 |
| `include/rkav/output` | PacketRouter 和输出端接口 |
| `src` | 对应模块实现 |
| `app/main.cpp` | 参数、信号和 Application 生命周期入口 |
| `config/mock.json` | 可直接运行的基线配置 |
| `tests/unit` | 不依赖真实时间和设备的单元测试 |
| `tests/integration` | 整条 Mock 管道测试 |
| `deploy` | systemd 服务文件 |
| `docs` | 架构、开发手册、配置和阶段状态 |

## 配置与日志

配置采用严格字段检查。字段拼错、类型错误、范围错误、选择未编译后端或没有可用输出
都会在启动线程前失败。详细字段见
[配置说明](docs/Project%20Execution%20Log/04-configuration.md)。

日志每行是一个 JSON 对象，可按 `module`、`event`、`level` 搜索。周期指标包括总帧数、
包数、错误、恢复次数、阶段延迟分位数和各队列高水位/丢弃数。禁止在每帧路径打印
INFO 日志，以免日志 I/O 干扰实时链路。

## 后续接真实硬件

接入顺序固定为 V4L2 摄像头、ALSA 麦克风、RKNN、RGA/MPP、实际封装与网络输出；一次
只替换一个 Mock 后端，并保持全部现有测试通过。Rockchip SDK 类型不得进入
`rkav_core` 公共头文件。当前限制和下一步见
[开发状态](docs/Project%20Execution%20Log/03-current-development-status.md)。
