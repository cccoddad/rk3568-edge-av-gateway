# RKAV 项目交接文档

## 项目定位与目标

**RK3568 实时音视频边缘分析网关**——在 RK3568 ARM64 嵌入式 Linux 上实时采集摄像头视频和麦克风音频，通过 NPU（RKNN）进行 YOLOv5 目标检测推理，叠加 OSD，编码为 H.264/AAC，最终通过 MP4 或 RTSP 输出。

项目采用"Mock 后端可重复回归 + 真实硬件逐步替换"的渐进式架构，一次只替换一个 Mock 后端并保持全部现有测试通过。

## 技术栈

| 项目 | 规格 |
|---|---|
| C++ 标准 | **C++20**（`CMAKE_CXX_STANDARD 20`，无扩展） |
| 构建系统 | **CMake 3.20+** + **Ninja** |
| 第三方库 | nlohmann/json 3.11.3、GoogleTest、libjpeg-turbo 3.1.4.1 |
| 目标平台 | **RK3568 ARM64**（Buildroot / Debian），Linux 内核 5.10 |
| 可选硬件 SDK | RKNN 1.4.0（NPU 推理）、MPP（硬件编码）、librga（色彩空间转换） |
| 可选软件 SDK | FFmpeg（H.264/AAC 软编码 + MP4 封装） |
| 真实采集 | V4L2（摄像头，MJPEG/YUYV）、ALSA 内核 UAPI（音频，S16_LE） |
| 静态分析 | clang-format（Google 风格 + 4 空格缩进）、clang-tidy、-Werror |

## 目录结构

```
├── app/main.cpp                 # 进程入口：CLI 解析、信号、Application 生命周期
├── include/rkav/                # 公共头文件（按模块分目录）
│   ├── common/                  # Error、Result、Buffer、Clock、Logger、Types
│   ├── capture/                 # IVideoCapture、IAudioCapture 接口 + Mock/V4L2/ALSA 声明
│   ├── vision/                  # IInferenceEngine 接口、Geometry、Overlay、RKNN 声明
│   ├── media/                   # IVideoEncoder/IDecoder、Checksum/FFmpeg/MPP 编码器、JPEG 解码器
│   ├── output/                  # IPacketSink、PacketRouter
│   ├── config/                  # AppConfig 强类型配置结构体
│   ├── queue/                   # BoundedQueue<T> 有界跨线程队列
│   ├── monitor/                 # Metrics 指标仓库
│   └── app/                     # Application 管道编排
├── src/                         # 实现代码（与 include 对应）
│   ├── common/                  # clock.cpp, error.cpp, logger.cpp, types.cpp
│   ├── config/config.cpp        # JSON → 强类型配置，严格字段和语义校验
│   ├── capture/mock/            # MockVideoCapture、MockAudioCapture
│   ├── capture/v4l2/            # V4L2VideoCapture（内核 UAPI MMAP）
│   ├── capture/alsa/            # AlsaAudioCapture（内核 PCM UAPI）
│   ├── vision/mock/             # MockInferenceEngine
│   ├── vision/rknn/             # RknnInferenceEngine（RKNN 1.4.0 生命周期）
│   ├── vision/cpu/cpu_overlay.cpp  # CPU OSD（检测框 + 文字叠加）
│   ├── media/mock/checksum_encoder.cpp  # Checksum 测试编码器
│   ├── media/jpeg/              # JpegVideoDecoder（libjpeg-turbo TurboJPEG API）
│   ├── media/ffmpeg/            # FfmpegVideoEncoder、FfmpegAudioEncoder
│   ├── media/mpp/               # MppRgaVideoEncoder（MPP + RGA 硬件编码）
│   ├── output/                  # PacketRouter、NullSink、JsonlSink、H264Sink、Mp4Sink
│   ├── monitor/metrics.cpp      # 周期指标采集
│   └── app/application.cpp      # 6 worker 线程管道组装、停止顺序、健康检测
├── tests/
│   ├── unit/                    # 单元测试（core、queue、config、overlay、mock 等）
│   ├── integration/             # Application 整条 Mock 管道集成测试
│   └── soak/                    # 长稳测试脚本
├── config/                      # JSON 配置文件
│   ├── mock.json                # PC 基线（全 Mock，640x360@30fps）
│   ├── rk3568-v4l2.json         # 板端 V4L2 + Mock 推理
│   ├── rk3568-rknn-mjpeg.json   # 板端 V4L2 + RKNN 推理
│   ├── rk3568-rknn-mjpeg-alsa.json       # 三硬件 + RKNN
│   ├── rk3568-rknn-mjpeg-alsa-mpp-h264.json  # 三硬件 + MPP 编码
│   ├── mock-ffmpeg-mp4.json     # PC FFmpeg MP4 软件基线
│   └── rk3568-rknn-mjpeg-alsa-ffmpeg-mp4.json  # 板端 FFmpeg MP4
├── cmake/
│   ├── Options.cmake            # 编译选项、Sanitizer、告警级别
│   └── Toolchains/              # aarch64 交叉编译工具链文件
├── tools/                       # 构建、测试、部署脚本
├── deploy/                      # systemd 服务文件
├── docs/                        # 架构文档、阶段交接、问题汇总（64 份）
├── CMakeLists.txt               # 主构建定义
├── CMakePresets.json            # 预设（debug/asan/release/cross-aarch64-static）
├── .clang-format                # Google 风格 + 4 空格缩进
├── .clang-tidy                  # bugprone/concurrency/modernize/performance 检查
└── README.md                    # 项目概览与快速开始
```

## 构建命令

### Windows / PowerShell（本机调试）

仓库目录名含非 ASCII 字符，脚本会在 `%LOCALAPPDATA%/rkav-gateway` 下建立短路径 Junction：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_windows.ps1
```

要求：CMake 3.20+、Ninja、支持 C++20 的 MSVC/Clang/GCC、Git。

### Ubuntu / RK3568 Debian（本机构建 + 测试）

```bash
sudo apt update && sudo apt install -y build-essential cmake ninja-build git
sh ./tools/build_and_test.sh
```

手动操作：

```bash
cmake --preset debug                    # 配置（Mock + 测试）
cmake --build --preset debug -j4        # 编译
ctest --preset debug                    # 运行 49 项测试
./build/debug/rkav-gateway --validate-config --config config/mock.json  # 校验配置
./build/debug/rkav-gateway --config config/mock.json                    # 运行 10 秒
```

### RK3568 交叉编译（AArch64 静态）

```bash
cmake --preset cross-aarch64-static     # 含 V4L2 + ALSA，不带 RKNN
cmake --build --preset cross-aarch64-static -j4
```

带 RKNN 的板端完整构建需要额外设置 `RKNN_SDK_ROOT`、`RKAV_MPP_HEADERS_ROOT`、`RKAV_RGA_HEADERS_ROOT`，参见 `tools/build_rknn_gateway.sh`。

### ASan + UBSan（仅 Linux）

```bash
cmake --preset asan
cmake --build --preset asan -j4
ctest --preset asan
```

## 运行与测试方法

### 基本运行

```bash
./build/debug/rkav-gateway --help                 # 查看帮助
./build/debug/rkav-gateway --version              # 查看版本
./build/debug/rkav-gateway --validate-config --config config/mock.json  # 只校验不启动
./build/debug/rkav-gateway --config config/mock.json --duration 60      # 运行 60 秒
./build/debug/rkav-gateway --config config/mock.json --duration 0       # 持续运行到 SIGINT/SIGTERM
```

### 测试

| 命令 | 用途 |
|---|---|
| `ctest --preset debug` | 运行全部单元 + 集成测试 |
| `ctest --preset asan` | ASan/UBSan 下运行测试 |
| `sh ./tools/soak_test.sh 1800` | 30 分钟长稳 + 资源采集 |
| `sh ./tools/signal_test.sh` | SIGINT/SIGTERM 退出验证 |

### 板端验收

完整板端验收步骤见 `docs/08-RK3568开发板M5阶段验收.md`。

退出码约定：`0`=成功，`1`=运行阶段失败，`2`=命令行或配置错误。

## 代码规范与已有约定

### 编码风格

- **格式化**：Google 风格基础 + 4 空格缩进 + 100 列限制 + 指针左对齐 + 大小写敏感排序 include（`.clang-format`）
- **命名规范**（`.clang-tidy`）：
  - 命名空间：`lower_case`
  - 类：`CamelCase`
  - 函数：`CamelCase`
  - 变量：`lower_case`
  - 私有成员后缀：`_`（如 `config_`、`state_`）
- **告警级别**：`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor -Werror`
- **注释**：每个 `.cpp` 文件首行用 `// 文件作用：` 和 `// 主要知识点：`；函数用 `/// 功能：` 和 `/// 返回：`

### 架构约定

- **rkav_core** 只放平台无关类型、算法和配置，**不依赖** V4L2/RKNN 等硬件 SDK
- **Rockchip SDK 类型不得进入 `rkav_core` 公共头文件**
- Mock 后端单独成库（`rkav_mock`），真实后端可并存而无需删除测试后端
- 每个跨线程队列都有固定容量和溢出策略（`drop_oldest` / `keep_latest` / `block_producer`）
- 配置采用严格字段检查：字段拼错、类型错误、范围错误、选择未编译后端都会在启动线程前失败
- 日志为 JSON 格式，可按 `module`/`event`/`level` 搜索；**禁止在每帧路径打印 INFO 日志**

### Git 约定

- 禁止 `git reset --hard`、`git checkout --`、`git clean`、force push
- 提交排除：密钥、Token、模型、SDK 二进制、数据库、照片、录像、板端日志、构建目录
- 每完成一个完整工作单元后更新 `docs/19-项目当前开发状态.md` 和最新交接文档

### 硬件安全边界

- 摄像头、麦克风和 NPU 由板端网关独占，不启动第二个 `rkav-gateway`
- 停止/重启服务、重启板卡、物理拔插、修改分辨率/FPS/ALSA/编码/RTSP 端口前需确认
- 测试使用唯一结果目录并保留运行前后证据；失败后先保存日志和 SHA-256，不盲目重跑

## 当前未完成事项

按固定后续路线（以 `docs/19-项目当前开发状态.md` 为准）：

1. **MPP/RGA 硬件编码板端实测** — 修复候选已暂存 WinDownloads（P115/docs/67），待板端复测
2. **RTSP 网络输出** — 已实现并在 PC 完成验证（推流/断连恢复/传输/超时/长稳/docs/68-71），板端待 MPP 通过后验证
3. **systemd 服务化** — `deploy/rkav-gateway.service` 已通过 `systemd-analyze verify`，完整 2/12 小时长稳待验证
4. **ZLMediaKit 流媒体服务层** — 阶段 1 PC 验证与 aarch64 部署包已完成（docs/73/74），板端部署（阶段 2）排在 MPP/RTSP 板端验证之后
5. **GEC-V11 板级迁移** — 5.10.209 内核 DTB 迁移评估进行中（docs/29-64）
6. **USB 断连** — 仍是安全退出后重启，非进程内自动热重连

> 注意：固定后续路线为"收口三硬件长稳 → USB 断连 → FFmpeg MP4 软件基线 → OSD → MPP/RGA → RTSP 与恢复 → systemd 及 2/12 小时完整长稳"，不要用其他项目改变顺序。
>
> 最新会话交接见 [docs/75-本次会话实现总结、问题归档与下一步交接.md](docs/75-本次会话实现总结、问题归档与下一步交接.md)，内含给下一段对话的现成提示词；下一步操作、证据索引与硬性协作规则以该文件与 `docs/19` 为准。
