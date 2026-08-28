# RK3568 实时音视频边缘分析网关

这是按《开发全流程手册》实现的第一版正式工程。Mock 后端用于可重复回归，Linux 上已经
加入并实机验证真实 V4L2 摄像头和 ALSA 麦克风后端；RKNN YOLOv5 推理后端已经实现为可选
模块。默认配置仍保留 Checksum 编码与输出，另有可选 FFmpeg H.264/AAC 编码和 MP4 输出软件
基线。硬件替换不修改公共数据契约和 Application 主流程。

使用 AI/Codex 继续开发前必须先阅读根目录 [AGENTS.md](AGENTS.md)。该文件定义项目范围、
工作区保护、开发板操作边界、Git 规则、验收证据和固定后续顺序；动态进度仍以 `docs/19` 和
最新编号交接文档为准。

## 当前完成度

- M0 工程基线：CMake targets、presets、严格编译告警、格式和静态检查配置。
- M1 公共核心：Error/Result、Buffer、Frame/Packet、单调时钟、时间戳换算、有界队列。
- M2 Mock 数据：RGB 测试图、连续正弦波、可配置视频失败、XRUN 和设备失联。
- M3 多线程管道：Mock 推理、Checksum Encoder、PacketRouter、Null/JSONL Sink。
- M4 工程能力：强类型配置、结构化日志、指标、工作线程健康检查和故障退出。
- M6 推理接入：RKNN 1.4.0 生命周期、MJPEG 解码、RGB/BGR 缩放、YOLOv5 后处理和来源坐标映射。
- M7 软件媒体基线：FFmpeg H.264/AAC 编码、MP4 封装、`.part` 临时文件原子提交和 `ffprobe` 验收。
- 自动测试：44 项单元与集成测试，覆盖正常、慢推理、限速、坏 JPEG 恢复、故障场景和媒体配置契约。

ARM64 静态 Mock 已在 RK3568 Buildroot 完成 33 项当前 PC 回归基线、板端 30 项旧基线、
信号退出、30 分钟和 2 小时长稳。绿联 2K 摄像头已完成 UVC/UAC 枚举、720p MJPEG 样本、
48 kHz 双声道 WAV 和 300 帧持续采集验收。第一版 V4L2 MMAP 后端已完成 ARM64 交叉编译，
并在板端通过 60 秒 Application 联调：1,741 帧、约 29.0 FPS、零错误和零队列丢弃。ALSA
内核 UAPI 后端也已完成 ARM64 交叉编译和板端双路联调：10 秒采集 500 个音频块和 285 帧
视频；30 分钟采集 90,000 个音频块和 52,340 帧视频，RSS 稳定为 9,728 KiB，错误、恢复和
队列丢弃均为 0，SIGINT/SIGTERM 也能排空队列并正常退出。独立 RKNN 1.4.0 MobileNet 和
YOLOv5s 已在板端完成真实 NPU 推理，YOLOv5 已验证固定图、摄像头单帧及连续 10 次基线。
主程序 Mock RGB + RKNN 已在板端完成 10 秒验证，51 次请求与结果全部完成，NPU 中断增加
51 次且无新增相关内核错误。主程序的 libjpeg-turbo MJPEG 解码预处理也已实现，并通过真实
1280x720 摄像头 JPEG 的 Windows Debug/Release 回归。板端真实 MJPEG + RKNN 10 秒链路也已
通过：269 帧视频、49 次解码/推理/NPU 中断、零错误、零队列丢弃和零新增相关内核错误。
随后 60 秒稳定性测试也通过：61.17 秒采集 1,660 帧（27.14 FPS），291 次解码、推理结果
和 NPU 中断完全对应，峰值温度 51.875 摄氏度，仍为零错误、零恢复和零队列丢弃。
真实 ALSA 麦克风加入后，首次联合测试发现 PCM 在 RKNN 初始化前过早启动，约 80 ms 的
缓冲区在读取线程启动前溢出。现已把 ALSA `START` 推迟到第一次 `Read()`；修复版三硬件
10 秒测试得到 276 帧视频、500 个音频块和 49 次解码/推理/NPU 中断，错误、恢复、线程错误、
队列丢弃和新增相关内核错误均为 0。30 分钟三硬件联合长稳也已收口：50,237 帧视频、89,983
个音频块、8,714 次推理结果和 NPU 中断全部守恒，RSS 工作态稳定为 57,652 KiB、线程数稳定为
9、峰值温度 57.222 摄氏度，错误、恢复、线程错误、队列丢弃和停止后残留均为 0。原控制器把
有上限的解码延迟滑动窗口样本数误当成累计解码次数，因而写出 `failed:1`；项目保留该原始记录，
并在五份原始证据 SHA-256 校验通过后按修正规则完成重验收。
真实 RKNN 三硬件链路的 USB 断连与重插恢复也已通过：拔出同一台 UGREEN USB 复合设备后，
视频和音频节点同时消失，网关无需外部强停便在约 3.27 秒后安全退出，全部队列关闭且无丢弃/
残留；重新插入后再次运行 10 秒，取得 279 帧视频、500 个音频块和 49 次推理/NPU 中断，
错误、恢复和工作线程错误均为 0。当前恢复语义仍是安全退出后重新启动，不是进程内自动热重连。
Ubuntu 主机上的 5 秒 Mock FFmpeg 软件媒体基线也已通过：产出实际 H.264/AAC MP4，`ffprobe`
确认 320x180 视频、48 kHz 双声道音频、5.020 秒时长、单调 PTS/DTS 和 0 ms 起始音画偏移。
该结果不等于 RK3568 上的 FFmpeg/MPP 实机媒体输出，也不包含 RTSP；默认 Checksum packet
仍不是 H.264/AAC 成品。

## 数据流

```text
Mock/V4L2VideoCapture -> video queue -> ChecksumVideoEncoder -------+
                      -> inference queue -> optional MJPEG Decoder  |
                                         -> Mock/RKNN Inference     |
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
Ninja、支持 C++20 的 GCC/Clang/MSVC，以及 Git。首次构建会下载并校验锁定版本的
libjpeg-turbo，同时下载锁定版本的 nlohmann/json 和 GoogleTest。

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
[M5 板端验收](docs/08-RK3568开发板M5阶段验收.md)。

当前粤嵌 Buildroot 的交叉编译、直连网络、TF 卡部署、实测指标和下一步见
[Buildroot 交叉编译与 RK3568 上板阶段总结](docs/09-Buildroot交叉编译与RK3568开发板部署总结.md)。

真实绿联摄像头和麦克风的 VID/PID、格式、样本与持续采集证据见
[绿联 2K USB 音视频设备验收](docs/11-绿联摄像头与麦克风验收.md)。

与板端 Runtime 1.4.0 匹配的开发文件、独立 AArch64 冒烟程序和验收边界见
[RKNN 1.4.0 独立冒烟测试](docs/15-RKNN运行时独立冒烟测试.md)。

YOLOv5s 转换、真实摄像头单帧检测、连续推理基线和主程序后端接入状态见
[RKNN YOLOv5 主程序后端接入](docs/17-RKNN-YOLOv5主程序后端接入.md)。

主程序 MJPEG 解码实现、PC 侧证据和下一次板端验收边界见
[MJPEG 解码预处理接入与验收](docs/22-MJPEG解码预处理接入与验收.md)。

本轮完整实现清单、30 分钟证据、验收规则修正和固定后续顺序见
[真实三硬件联合验收与 ALSA 启动修复总结](docs/23-真实三硬件联合验收与ALSA启动修复总结.md)。
真实 RKNN 链路的 USB 断连、安全退出、重插重启证据和当前恢复边界见
[USB 断连与重插恢复验收](docs/24-USB断连与重插恢复验收.md)。
较早的 RKNN 接入过程见
[本次会话实现总结与下一步](docs/18-本次会话实现总结与下一步.md)。

## 目录说明

| 路径 | 职责 |
|---|---|
| `include/rkav/common` | 公共错误、内存、时间和数据契约 |
| `include/rkav/capture` | 视频/音频采集接口及 Mock 声明 |
| `include/rkav/vision` | 推理接口和图像坐标变换 |
| `include/rkav/media` | 视频解码、编码接口和 Checksum 测试编码器 |
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
[配置说明](docs/04-项目配置说明.md)。

日志每行是一个 JSON 对象，可按 `module`、`event`、`level` 搜索。周期指标包括总帧数、
包数、错误、恢复次数、阶段延迟分位数和各队列高水位/丢弃数。禁止在每帧路径打印
INFO 日志，以免日志 I/O 干扰实时链路。

## 后续接真实硬件

接入顺序固定为 V4L2 摄像头、ALSA 麦克风、RKNN、RGA/MPP、实际封装与网络输出；一次
只替换一个 Mock 后端，并保持全部现有测试通过。Rockchip SDK 类型不得进入
`rkav_core` 公共头文件。当前限制和下一步见
[开发状态](docs/19-项目当前开发状态.md)。
