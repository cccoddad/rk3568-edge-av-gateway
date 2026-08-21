# Buildroot 交叉编译与 RK3568 板端运行总结

更新日期：2026-08-21

## 1. 本文定位

本文记录项目从“仅在 Windows Mock 环境验证”推进到“程序实际运行在粤嵌 RK3568
Buildroot 板卡”的完整实现、证据和后续计划。

本阶段没有接入摄像头、麦克风、RKNN、RGA 或 MPP。已经证明的是：现有 C++20 Mock
管道可以交叉编译成 Linux ARM64 程序，部署到旧版 Buildroot，并完成配置校验和 10 秒双流
运行。真实硬件后端仍需逐个实现。

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

### 3.4 串口、直连网口与 SSH 部署链路

实际建立的控制链路：

```text
USB 串口 COM5，1500000 8-N-1
    -> 中断厂商前台 demo，进入 root shell
    -> 启用 eth0 并配置 192.168.50.2/24

Windows 有线网卡 192.168.50.1/24
    -> ping 192.168.50.2
    -> TCP 22 端口检查
    -> SecureCRT SSH2 / scp
```

串口不依赖 IP，适合首次启动、网络配置和故障恢复；SSH 依赖 IP 和 22 端口，适合日常操作
和传输文件。PowerShell 的 `ping` 证明三层 IP 可达，`Test-NetConnection -Port 22` 证明 SSH
服务端口可达。

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

ARM64 静态程序通过 SCP 上传到 `/root/rkav`，板端已执行：

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

当前留存输出没有包含 `rkav_tests` 的最终 `[ PASSED ] 30 tests` 汇总，因此文档只声明网关
运行通过。板端 30 项测试仍需重新执行并保存日志。

## 4. 本阶段解决了什么

本阶段把项目从“代码理论上支持 Linux”推进为“同一套公共代码实际运行在 RK3568 Linux”：

1. 识别了目标系统不是 Ubuntu，而是缺少开发工具的旧 Buildroot。
2. 没有为方便开发而立即刷机，先保留厂商 BSP 和外设适配。
3. 建立了可重复的 ARM64 静态交叉编译入口。
4. 解决了交叉编译下 GoogleTest 不能在构建机执行的问题。
5. 使用 `file` 在部署前校验目标架构和链接方式。
6. 建立串口恢复、网线直连、SSH 控制和 SCP 部署链路。
7. 在真实 RK3568 上取得配置校验、吞吐、延迟、队列和正常停止证据。

## 5. 当前边界与遗留风险

- 板端 30 项 GoogleTest 结果尚未形成可留档证据。
- SIGINT、SIGTERM 进程级验收尚未在当前 Buildroot 路线上留档。
- 尚未执行 30 分钟和 2 小时长稳，RSS、CPU、温度趋势未知。
- 板卡 RTC 节点不可用，日志墙上时间为 1970 年；内部单调 PTS 不受影响，但证据时间不可靠。
- 厂商 `reboot` 流程疑似卡在 `usbdevice stop`，需定位对应初始化脚本。
- 当前 Buildroot 没有 systemd，不能使用现有 systemd unit 完成该项验收。
- 静态链接适合当前纯 C++ Mock 程序；RKNN、RGA、MPP 等厂商库通常必须使用 SDK 对应的
  sysroot、头文件和动态库，不能继续假设通用静态工具链足够。
- 当前仍没有真实摄像头、麦克风、NPU 推理和 H.264/AAC 输出。

## 6. 接下来怎么做

### 6.1 先完成 Buildroot 版 M5 基线

1. 冷启动板卡，验证 `/etc/network/interfaces` 中的静态 IP 是否自动生效。
2. 修正系统时间或记录“RTC 不可用”，避免后续日志全部显示 1970 年。
3. 执行并保存板端测试：

   ```bash
   cd /root/rkav
   ./rkav_tests --gtest_color=no > tests.log 2>&1
   echo $? > tests.exit_code
   tail -n 10 tests.log
   ```

4. 分别发送 SIGINT、SIGTERM，检查停止原因、退出码、队列排空和最大退出耗时。
5. 编写或适配 BusyBox `/proc` 版本的长稳采样脚本，执行 30 分钟 Mock 长稳。
6. 记录 RSS、CPU、温度、错误数、队列高水位和丢弃数。
7. 定位 `usbdevice stop` 卡顿；在解决前，关机前先 `sync` 并保留串口日志。

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
从运行时发现改成源码登记，产物再放到板上执行。通过串口配置直连网络，使用 SSH/SCP 部署，
最终在 RK3568 上完成 10 秒 Mock 双流运行：约 30 FPS 视频、每秒 50 个音频块、801 个包
完整路由消费、错误和丢弃均为 0，停止后队列全部排空。下一步是补齐板端测试、信号和长稳
证据，再用厂商 SDK 逐个接入 V4L2、ALSA、RKNN 和 MPP。

## 8. 相关文档

- [整体代码架构](./01-overall-code-architecture.md)
- [项目实现总结与后续计划](./05-项目实现总结与后续计划.md)
- [项目问题汇总：面试版](../06-项目问题汇总-面试版.md)
- [项目问题汇总：通俗版](../07-项目问题汇总-通俗版.md)
- [M5 RK3568 板端验收](./08-m5-board-validation.md)
