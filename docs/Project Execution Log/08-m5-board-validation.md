# M5 RK3568 板端验收

本文只记录和执行 M5 的 Mock 板端验收。它不代表 V4L2、ALSA、RKNN、RGA、MPP、MP4 或
RTSP 已完成；这些后端必须在 M5 完成后按既定顺序逐个接入。

## 前置条件

- 已在目标 RK3568 Linux 板上检出本仓库，`uname -m` 应为 `aarch64`。
- 可用命令：C++20 编译器、CMake 3.20+、Ninja、Git、POSIX `sh`、`ps`、`grep`。
- 板端有足够可写空间供 `build/` 和 `out/` 使用；2 小时长稳前应保证散热和稳定供电。
- 默认脚本不访问摄像头或麦克风，也不需要 Rockchip SDK。

安装依赖的示例：

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git
```

## 一键验收

先执行 30 分钟验收：

```bash
sh tools/m5_board_validation.sh 1800
```

脚本依次采集环境基线、构建并运行 Debug 测试、验证 SIGTERM、运行 ASan/UBSan、构建
Release，并执行长稳。所有证据写入 `out/m5-<时间>/`：

| 证据 | 内容 |
|---|---|
| `baseline/system.txt` | 内核、系统、CPU、内存、磁盘和工具链原始输出 |
| `baseline/thermal.txt` | 所有可读 thermal zone 的原始温度值 |
| `debug-build-and-test.log` | Debug 构建、30 项测试和配置校验 |
| `signal/gateway.log` | SIGTERM 后的 `application_stopped` 日志 |
| `sanitizers.log` | ASan/UBSan 构建及测试结果 |
| `soak-*/resources.csv` | RSS、CPU、温度的周期采样 |
| `soak-*/gateway.log` | 周期指标、队列高水位和最终指标 |

温度默认读取第一个可读的 `/sys/class/thermal/thermal_zone*/temp`。若该节点不是 CPU 温度，
应先检查 `baseline/thermal.txt`，再显式指定正确节点：

```bash
RKAV_TEMPERATURE_PATH=/sys/class/thermal/thermal_zone0/temp sh tools/soak_test.sh 1800
```

运行完成后，将实际板卡型号、镜像、工具链和证据路径填回
`docs/environment/rk3568-baseline-template.md`。不要把 PC 或模拟器结果填入该文件。

## systemd 验收

M5 的系统服务需要管理员权限，因此不纳入无特权的一键脚本。先以 Release 安装到 unit
引用的位置，并创建服务账号和可写目录：

```bash
sudo cmake --install build/release --prefix /opt/rkav
sudo install -D -m 0644 deploy/rkav-gateway.service /etc/systemd/system/rkav-gateway.service
sudo useradd --system --no-create-home --shell /usr/sbin/nologin rkav 2>/dev/null || true
sudo install -d -o rkav -g rkav /var/lib/rkav
sudo systemctl daemon-reload
sudo systemctl enable --now rkav-gateway.service
sudo systemctl status rkav-gateway.service --no-pager
sudo journalctl -u rkav-gateway.service -b --no-pager
```

该 unit 使用 `/opt/rkav/etc/rkav/mock.json`，它由 CMake 安装规则提供。验证停止与启动：

```bash
sudo systemctl stop rkav-gateway.service
sudo systemctl start rkav-gateway.service
sudo systemctl is-active --quiet rkav-gateway.service
```

服务在 `Restart=on-failure` 下的重启验证应在维护窗口执行，并保留 `journalctl` 输出；正常
`systemctl stop` 不应触发重启。通过后再执行：

```bash
sh tools/m5_board_validation.sh 7200
```

## M5 完成判定

- Debug 和 ASan/UBSan 测试均通过，配置校验通过。
- SIGTERM 返回 0，日志含有停止原因 `signal`，无关闭超时警告。
- `resources.csv` 中 RSS 没有持续上升，CPU 与温度处于该板卡散热条件允许的范围。
- `gateway.log` 中的队列 `high_watermark` 未超过 `capacity`，没有未知错误或 worker 卡死。
- systemd 可启动、停止并在失败时遵守配置的重启策略，2 小时长稳保留完整证据。

任何失败都应保留上述目录，并同步更新 [项目问题汇总：面试版](../06-项目问题汇总-面试版.md)
与 [项目问题汇总：通俗版](../07-项目问题汇总-通俗版.md)，记录命令、环境、现象、根因和
修复后的证据。
