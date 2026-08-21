# RK3568 板卡环境基线模板

本文件必须在实际板卡上填写。不要根据卖家页面猜测结果，也不要把 PC 验证结果写成
板卡结果。

## 基本信息

| 项目 | 实测值 |
|---|---|
| 板卡品牌与完整型号 | 待填写 |
| PCB/硬件版本 | 待填写 |
| SoC | RK3568（需命令确认） |
| 内存容量 | 待填写 |
| 存储介质与容量 | 待填写 |
| 电源适配器规格 | 待填写 |
| 系统镜像来源与版本 | 待填写 |
| 测试日期 | 待填写 |

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
| 内核版本 | 待填写 |
| 架构（应为 aarch64） | 待填写 |
| GCC/Clang 版本 | 待填写 |
| CMake/Ninja 版本 | 待填写 |
| 空闲内存 | 待填写 |
| 根分区剩余空间 | 待填写 |
| 空闲温度 | 待填写 |

## 构建与运行结果

| 检查项 | 结果/证据路径 |
|---|---|
| Debug 构建 | 待填写 |
| 30 项测试 | 待填写 |
| 默认 10 秒运行 | 待填写 |
| SIGTERM 有界退出 | 待填写 |
| 30 分钟长稳 | 待填写 |
| 2 小时长稳 | 待填写 |
| systemd 启停与失败重启 | 待填写 |
