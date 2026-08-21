# Buildroot 交叉编译与 RK3568 板端运行总结

更新日期：2026-08-21

## 1. 本文定位

本文记录项目从“仅在 Windows Mock 环境验证”推进到“程序实际运行在粤嵌 RK3568
Buildroot 板卡”的完整实现、证据和后续计划。

本阶段没有接入摄像头、麦克风、RKNN、RGA 或 MPP。已经证明的是：现有 C++20 Mock
管道可以交叉编译成 Linux ARM64 程序，部署到旧版 Buildroot，并完成配置校验、30 项板端测试、
信号退出和 10 分钟双流长稳。真实硬件后端仍需逐个实现。

## 2. 板卡环境识别

通过 1500000 波特率串口进入板卡，实际读取到：

```text
架构：aarch64
系统：Buildroot 2018.02-rc3
内核：Linux 4.19
主机名：RK356X
厂商工程：yueqian/rk356x/RK356X_Linux_V1.3.2
初始化系统：BusyBox/SysV init，存在 /etc/init.d/S40network
根文件系统：ext4，可读写
```

系统没有 `cmake`、`g++` 和 `git`，只有 `scp`。因此不能执行原计划中的板上原生编译，也
不能使用面向 Ubuntu/systemd 的完整 M5 脚本。

涉及知识点：

- `uname`、`/etc/os-release`、`which` 和挂载信息用于环境盘点。
- Buildroot 是生成精简嵌入式 Linux 固件的构建系统，不等同于 Ubuntu/Debian。
- BusyBox/SysV init 与 systemd 的服务管理方式不同。
- 板卡型号相同不代表 BSP、设备树和用户态 ABI 相同。

## 3. 实现内容

### 3.1 M5 验收工具骨架

仓库新增或完善：

- `tools/collect_baseline.sh`：采集内核、系统、CPU、内存、磁盘、工具链和 thermal zone。
- `tools/signal_test.sh`：向持续运行的进程发送 SIGTERM，检查退出码和停止原因。
- `tools/soak_test.sh`：长稳期间采集 RSS、CPU 和温度，并保留网关日志。
- `tools/m5_board_validation.sh`：编排原生 Debug、Sanitizer、Release、信号和长稳验收。
- `docs/08-m5-board-validation.md`：说明标准 Linux/systemd 环境下的 M5 操作。
- 修正 systemd unit 的安装后配置路径。

这些脚本仍适用于具备 CMake、编译器和常规 GNU/Linux 工具的板端环境。当前粤嵌 Buildroot
不具备这些条件，因此本阶段新增交叉编译路线作为兼容方案。

### 3.2 Linux ARM64 静态交叉编译

新增 `cmake/Toolchains/aarch64-linux-gnu-static.cmake`：

- 目标系统设置为 Linux，目标架构设置为 `aarch64`。
- C 编译器使用 `aarch64-linux-gnu-gcc`。
- C++ 编译器使用 `aarch64-linux-gnu-g++`。
- 查找库和头文件时只使用 ARM64 target sysroot，避免误链接 x86_64 主机库。
- 可执行文件使用静态链接，减少对旧 Buildroot `glibc`、`libstdc++` 版本的依赖。

`CMakePresets.json` 新增 `cross-aarch64-static` 配置和构建 preset，
`tools/build_aarch64_static.sh` 负责：

1. 检查交叉编译器、CMake、Ninja 和 `file`。
2. 配置并构建网关和测试程序。
3. 收集 `rkav-gateway`、`rkav_tests` 和 `mock.json`。
4. 使用 `file` 强制校验产物必须是 `ARM aarch64` 且 `statically linked`。

构建产物已经在 Ubuntu 24.04 VMware 中验证为：

```text
ELF 64-bit LSB executable, ARM aarch64, statically linked
```

涉及知识点：

- 交叉编译中的 build、host、target 三元关系。
- CMake toolchain file、sysroot 和 `CMAKE_FIND_ROOT_PATH_MODE_*`。
- 静态链接与动态链接、ABI 和运行库版本兼容。
- ELF 文件格式与 CPU 架构检查。

### 3.3 交叉编译测试注册

原项目使用 `gtest_discover_tests()`，它会在构建机上运行刚生成的测试程序以发现测试名。
交叉编译时生成的是 ARM64 ELF，而 VMware 是 x86_64，构建机不能直接执行目标程序。

实现调整为：

```text
原生构建：gtest_discover_tests()
交叉构建：gtest_add_tests()
```

交叉构建阶段从测试源码登记测试，真正的 `rkav_tests` 复制到 RK3568 后再运行。这保持了原生
开发体验，也避免使用模拟器掩盖目标机问题。

涉及知识点：

- CMake `CMAKE_CROSSCOMPILING`。
- GoogleTest 的构建后发现与源码扫描登记。
- “构建成功”与“目标机测试成功”是两份不同证据。

### 3.4 串口、直连网口与 SSH 故障诊断

实际建立的控制链路：

```text
USB 串口 COM5，1500000 8-N-1
    -> 中断厂商前台 demo，进入 root shell
    -> 启用 eth0 并配置 192.168.50.2/24

Windows 有线网卡 192.168.50.1/24
    -> ping 192.168.50.2
    -> TCP 22 端口检查
    -> Dropbear 监听 22 端口，但 SSH banner 握手仍超时
    -> 当前继续使用 SecureCRT 串口控制
    -> 小文件可由板卡通过 HTTP/wget 从 Windows 下载
```

串口不依赖 IP，适合首次启动、网络配置和故障恢复。PowerShell 的 `ping` 证明三层 IP 可达，
`Test-NetConnection -Port 22` 只证明 TCP 端口可达，不能证明 SSH 协议握手成功。板端 Dropbear
主服务和独立 2222 端口诊断服务都能接受来自 `192.168.50.1` 的 TCP 连接，但客户端在认证前等待
banner 超时，因此当前不能把 SSH/SCP 记为已验收链路。诊断服务已经关闭，只保留原 Dropbear 服务。

静态 IP 已写入 `/etc/network/interfaces`：

```text
auto eth0
iface eth0 inet static
    address 192.168.50.2
    netmask 255.255.255.0
```

但执行 `reboot` 时厂商系统停在 `usbdevice stop` 附近，因此永久 IP 仍需在下一次成功冷启动
后再次确认，不能仅凭配置文件已写入就宣称验收完成。

### 3.5 板端 Mock 管道实测

ARM64 静态程序已部署到 `/root/rkav`，板端已执行：

```bash
./rkav-gateway --validate-config --config mock.json
./rkav-gateway --config mock.json --duration 10
```

配置校验通过。一次完整 10 秒运行的最终证据为：

| 指标 | 实测值 | 说明 |
|---|---:|---|
| `video_captured_total` | 301 | 约 30 FPS |
| `video_packets_total` | 300 | 视频处理链路持续工作 |
| `audio_captured_total` | 501 | 20 ms/块，约 50 块/秒 |
| `audio_packets_total` | 501 | 音频块全部编码 |
| `packets_routed_total` | 801 | 音视频包合计 |
| `packets_consumed_total` | 801 | Sink 全部消费 |
| `errors_total` | 0 | 无运行错误 |
| 所有队列 `dropped` | 0 | 无丢弃 |
| 队列高水位 | 1～2 | 显著低于容量 |
| 停止原因 | `run_duration_elapsed` | 10 秒到期正常停止 |
| 停止后队列 | `closed=true,size=0` | 队列排空并关闭 |

延迟样例：Mock 推理 p95 约 20.7 ms，Checksum 视频编码 p95 约 12.4 ms，Checksum 音频编码
p95 约 57 us。这些是当前 Mock 工作负载的板端基线，不代表 RKNN、MPP 或真实音视频性能。

### 3.6 Buildroot 板端补充验收

随后完成了原总结中缺失的测试、信号和长稳证据。

板端 `rkav_tests` 结果：

```text
[==========] 30 tests from 12 test suites ran. (1782 ms total)
[  PASSED  ] 30 tests.
exit_code=0
```

测试日志中出现的 XRUN、断连和 fatal 信息来自测试用例主动注入的故障；最终 30 项全部通过，不能将
这些预期日志误判为板卡故障。

分别向无限运行模式发送 SIGTERM 和 SIGINT，两次结果均为：

| 项目 | SIGTERM | SIGINT |
|---|---:|---:|
| 退出码 | 0 | 0 |
| 秒级退出耗时 | 0 秒 | 0 秒 |
| 停止原因 | `signal` | `signal` |
| 错误、丢弃 | 0 | 0 |
| 停止后队列 | 已关闭、已排空 | 已关闭、已排空 |

为兼容缺少 systemd 和完整 GNU 工具的旧 Buildroot，新增
`tools/soak_test_buildroot.sh`。脚本只依赖 BusyBox、`/proc` 和 `/sys`，采集进程 RSS、线程数、
CPU 占用、SoC 温度与 CPU 频率，并在温度达到 85 摄氏度时终止测试。ARM64 产物打包脚本会将
它与 `rkav-gateway`、`rkav_tests` 和 `mock.json` 一起输出。

一次 600 秒、10 秒采样间隔的 Mock 长稳结果：

| 指标 | 实测结果 |
|---|---:|
| 脚本退出码 | 0 |
| 热保护触发 | 0 |
| CSV | 1 行表头 + 60 个样本 |
| RSS | 启动样本 2932 kB，稳定后 4376 kB |
| 线程数 | 9，保持稳定 |
| 进程 CPU | 预热后约 82%～86%，100% 代表占满一个核心 |
| SoC 温度 | 45.0～46.1 摄氏度 |
| CPU 频率 | 观察到 1.104 GHz 和 1.416 GHz |
| 音频捕获/编码 | 30001 / 30001 |
| 视频捕获/编码 | 18001 / 18001 |
| 推理请求/结果 | 18001 / 18001 |
| 路由/消费包 | 48002 / 48002 |
| 错误、恢复、过期检测 | 0 / 0 / 0 |
| 所有队列丢弃 | 0 |
| 停止后队列 | `closed=true,size=0` |
| 停止原因 | `run_duration_elapsed` |

延迟 p95 为：Mock 推理 20.636 ms、Checksum 视频编码 12.361 ms、Checksum 音频编码 57 us。
这些数字证明当前 Mock 管道在 RK3568 CPU 上的基线稳定性，不代表真实摄像头、麦克风、MPP、
RKNN/NPU 或产品级长时间运行性能。

## 4. 本阶段解决了什么

本阶段把项目从“代码理论上支持 Linux”推进为“同一套公共代码实际运行在 RK3568 Linux”：

1. 识别了目标系统不是 Ubuntu，而是缺少开发工具的旧 Buildroot。
2. 没有为方便开发而立即刷机，先保留厂商 BSP 和外设适配。
3. 建立了可重复的 ARM64 静态交叉编译入口。
4. 解决了交叉编译下 GoogleTest 不能在构建机执行的问题。
5. 使用 `file` 在部署前校验目标架构和链接方式。
6. 建立串口恢复和网线直连链路，并明确隔离 SSH 协议握手故障。
7. 在真实 RK3568 上取得配置校验、30 项测试、信号退出、吞吐、延迟、队列和 10 分钟长稳证据。

## 5. 当前边界与遗留风险

- 10 分钟 Mock 长稳已经通过，但尚未进行 30 分钟、2 小时或产品级长期运行。
- 板卡 RTC 初始为 1970 年，本次通过 `date -s` 临时设置到 2026-08-21；断电后的持久性未解决。
- Dropbear 监听端口并接受 TCP 连接，但 SSH banner 握手超时，当前仍以串口为主。
- 厂商 `reboot` 流程疑似卡在 `usbdevice stop`，需定位对应初始化脚本。
- 当前 Buildroot 没有 systemd，不能使用现有 systemd unit 完成该项验收。
- 静态链接适合当前纯 C++ Mock 程序；RKNN、RGA、MPP 等厂商库通常必须使用 SDK 对应的
  sysroot、头文件和动态库，不能继续假设通用静态工具链足够。
- 当前仍没有真实摄像头、麦克风、NPU 推理和 H.264/AAC 输出。

## 6. 接下来怎么做

### 6.1 完成剩余板卡管理项

1. 冷启动板卡，验证 `/etc/network/interfaces` 中的静态 IP 是否自动生效。
2. 明确 RTC 是否有电池和可写节点；解决前每次冷启动后校时。
3. SSH 不是接入真实硬件的前置条件，可单独调查 banner 超时，不阻塞主线。
4. 定位 `usbdevice stop` 卡顿；在解决前，关机前先 `sync` 并保留串口日志。
5. 30 分钟和 2 小时 Mock 长稳推迟到下一里程碑前执行，当前 10 分钟基线已经足够进入硬件盘点。

### 6.2 获取粤嵌配套 SDK

接真实硬件前向老师或厂商获取与当前板卡和 `RK356X_Linux_V1.3.2` 匹配的：

- Linux SDK/BSP、交叉工具链和 target sysroot。
- RKNN Runtime、RGA、MPP 的头文件、库和版本说明。
- 原理图、设备树、摄像头接口说明和原厂镜像恢复包。
- RKDevTool、驱动和烧录教程。

SDK 的目的不是替代当前应用架构，而是为真实硬件后端提供与驱动匹配的编译和运行环境。

### 6.3 按单变量原则接入真实后端

完成顺序保持不变：

```text
V4L2 摄像头 -> ALSA 麦克风 -> RKNN -> RGA -> MPP H.264 -> AAC -> MP4 -> RTSP
```

每次只增加一个后端，保留 Mock 回归，先做最小硬件测试，再做故障注入和 30 分钟长稳。

## 7. 面试介绍参考

我拿到的是粤嵌 RK3568 教学板，系统是 Buildroot 2018.02 和 Linux 4.19，没有 CMake、G++
和包管理器。为了保留厂商 BSP，我没有立刻刷 Ubuntu，而是在 x86_64 Ubuntu 中建立 ARM64
静态交叉编译。CMake toolchain 显式区分目标编译器和 target sysroot，交叉构建时把 GoogleTest
从运行时发现改成源码登记，产物再放到板上执行。通过串口配置直连网络，在 RK3568 上完成
30 项板端测试、SIGINT/SIGTERM 优雅退出和 10 分钟 Mock 双流长稳：48002 个包完整路由消费，
错误和丢弃均为 0，RSS 稳定在约 4.3 MiB，SoC 温度约 45～46 摄氏度，停止后队列全部排空。
SSH 端口可达但协议握手仍待处理。下一步使用厂商 SDK 逐个接入 V4L2、ALSA、RKNN 和 MPP。

## 8. 相关文档

- [整体代码架构](./01-overall-code-architecture.md)
- [项目实现总结与后续计划](./05-项目实现总结与后续计划.md)
- [项目问题汇总：面试版](../06-项目问题汇总-面试版.md)
- [项目问题汇总：通俗版](../07-项目问题汇总-通俗版.md)
- [板端联调阶段实现复盘与下一步](../10-板端联调阶段实现复盘与下一步.md)
- [M5 RK3568 板端验收](./08-m5-board-validation.md)
