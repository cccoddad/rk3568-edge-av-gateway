# RK3568 板卡环境基线与 M5 验收记录

本文件必须在实际板卡上填写。不要根据卖家页面猜测结果，也不要把 PC 验证结果写成
板卡结果。

## 基本信息

| 项目 | 实测值 |
|---|---|
| 板卡品牌与完整型号 | 粤嵌 RK356X 教学板；精确 PCB 型号待由丝印/原理图确认 |
| PCB/硬件版本 | 待填写 |
| SoC | RK3568 系列（运行环境主机名为 `RK356X`；精确型号待设备树确认） |
| 内存容量 | 待填写 |
| 存储介质与容量 | 待填写 |
| 电源适配器规格 | 待填写 |
| 系统镜像来源与版本 | `yueqian/rk356x/RK356X_Linux_V1.3.2`，Buildroot 2018.02-rc3 |
| 测试日期 | 2026-08-22 |

## 采集命令

```bash
uname -a
cat /etc/os-release
lscpu
free -h
df -hT
cat /proc/device-tree/model 2>/dev/null
cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null
cmake --version
c++ --version
ninja --version
```

把完整输出保存为 `out/baseline/<日期>/system.txt`，并在下表填写摘要。

| 项目 | 实测值 |
|---|---|
| 内核版本 | Linux 4.19（完整版本见板端原始基线输出） |
| 架构（应为 aarch64） | aarch64 |
| GCC/Clang 版本 | 板端未安装 C++ 编译器 |
| CMake/Ninja 版本 | 板端未安装 |
| 空闲内存 | 待补充原始 `free -h` 留档 |
| 根分区剩余空间 | 待补充原始 `df -hT` 留档 |
| 空闲温度 | 长稳期间约 45.6～47.8 C；待补充空闲采样 |

## 构建与运行结果

| 检查项 | 结果/证据路径 |
|---|---|
| Debug 构建 | x86_64 Ubuntu 交叉构建为 ARM64 静态产物；板端不做原生构建 |
| 30 项测试 | 通过，`30 tests from 12 test suites`，退出码 0；`/root/rkav/out/m5-30min-20260822-030420/tests.log` |
| 默认 10 秒运行 | 通过，801 个包全部消费，错误和丢弃为 0；见板端运行总结 |
| SIGTERM 有界退出 | 通过，退出码 0，停止原因 `signal`，队列排空 |
| 30 分钟长稳 | 通过，退出码 0，RSS 4360 kB、9 线程、0 错误/丢弃；`/root/rkav/out/m5-30min-20260822-030420/soak` |
| 2 小时长稳 | 通过，退出码 0，RSS 4376 kB、9 线程、576003 包完整消费、0 错误/丢弃；`/root/rkav/out/m5-2hour-20260822-060200/soak` |
| systemd 启停与失败重启 | 不适用：当前镜像为 BusyBox/SysV init |

## M5 管理项

- 静态 IP：已通过物理断电冷启动验证。板端为 `192.168.50.2/24`，Windows 为 `192.168.50.1/24`。
- RTC：未注册 `/dev/rtc*`；`rtc-pcf8563@0x51` probe 失败。每次冷启动后须临时校时。
- SSH：ARP、ICMP（Windows 到板端）和 TCP 22 均通过；OpenSSH 在 Dropbear banner 阶段超时，仍以串口为主。
- `poweroff`：已正常到达 `Power down`；`reboot` 的 `usbdevice stop` 停止路径问题待独立定位。
