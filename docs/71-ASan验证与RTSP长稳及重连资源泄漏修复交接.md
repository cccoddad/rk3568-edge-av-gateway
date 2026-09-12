# ASan/UBSan 验证、RTSP 30 分钟长稳与重连资源泄漏修复交接

更新日期：2026-09-12

## 1. 当前结论

1. **ASan + UBSan 全套测试通过**：FFmpeg ON 原生构建 73/73，零 AddressSanitizer 报告、
   零 LeakSanitizer 泄漏、零 UBSan 运行时错误；已用 CMake 缓存和 `ldd` 确认 sanitizer
   真实链接。
2. **30 分钟 PC RTSP 长稳发现并修复一个真实泄漏**：首轮长稳（每 30 秒收流 + 10 秒断连注入）
   中，文件描述符**每次重连 +1**（4→47），RSS 线性增长约 35 KB/分钟。根因是断连路径直接
   `avformat_free_context`，而 FFmpeg 不会替 muxer 调用 `write_trailer`，RTSP muxer 内部的
   TCP 连接（`rtsp_hd`）和 RTP 句柄只在 `rtsp_write_close` 中释放。
3. **修复后复测通过**：5 分钟探针 FD 恒定 4↔5（7 次重连零增长）；30 分钟复测退出码 0、
   43 次自动重连、零致命错误，RSS 后 20 分钟稳定在 40.4 MB（波动 ±14 KB），FD 恒定 4↔5，
   44 段捕获全部有效。
4. **2 小时长稳加长验证通过**：同一修复版本连续运行 7200 秒，175 次断连全部自动恢复，
   退出码 0、零致命错误；RSS 从预热后的 40.88 MB 到结束 40.90 MB（90 分钟内 +18 KB，
   约 0.2 KB/分钟，无线性趋势），FD 恒定 4↔5，176 段捕获写入完成。

## 2. ASan/UBSan 验证

| 项目 | 值 |
|---|---|
| 配置 | `-DRKAV_ENABLE_ASAN=ON -DRKAV_ENABLE_UBSAN=ON`，Debug，FFmpeg ON，测试 ON |
| 运行选项 | `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1`、`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1` |
| 结果 | 73/73 通过；`asan_errors=0`、`lsan_leaks=0`、`ubsan_errors=0` |
| 生效确认 | `CMakeCache.txt` 两个开关 ON；`ldd` 显示 `libasan.so.8`、`libubsan.so.1` |
| 证据目录 | 共享目录 `asan-e2e-20260912-165223`，`ctest.log` SHA-256 `cf5528c4...b705f` |

## 3. 长稳设计与首轮结果（修复前）

设计：网关推流到本地 RTSP 接收端；接收端循环“捕获 30 秒 → 断开 10 秒”，持续 1800 秒；
每 10 秒采样 RSS/CPU/FD 与重连计数。

| 指标 | 首轮结果（修复前） |
|---|---|
| 网关退出码 | 0 |
| 重连 | `reconnected=43`、`connection_lost=44`（最后一次断连在运行结束前未恢复，符合预期） |
| 致命错误 | 0 |
| 文件描述符 | 4 → 47，**每次重连 +1** |
| RSS | 41.1 → 41.9 MB 线性增长（约 35 KB/分钟） |
| 证据目录 | 共享目录 `rtsp-soak-30m-20260912-170229` |

短探针（3 分钟、4 次重连）进一步确认：FD 4→5→6→7→8→9 与重连次数一一对应。

## 4. 根因与修复

**根因**：`avformat_free_context` 只释放格式上下文结构，不会调用 muxer 的收尾函数；RTSP
muxer 的私有状态（`RTSPState`）里持有 TCP URLContext（socket）和每路 RTP 句柄，这些资源
只在 `av_write_trailer → rtsp_write_close → ff_rtsp_close_streams/close_connections` 中释放。
原实现的断连路径（写入失败、Abort 关闭）跳过 trailer 直接释放上下文，因此每次重连泄漏
一个 socket 及相关内存。

**修复**（`src/output/ffmpeg/ffmpeg_rtsp_sink.cpp`）：

- `ReleaseFormat()`：若会话仍处于 `connected` 状态，先启动超时守卫并调用
  `av_write_trailer` 完成协议收尾，再释放上下文；
- `Flush()`：显式 trailer 成功后置 `connected=false` 再 `ReleaseFormat()`，避免重复收尾；
  trailer 失败时保留 `connected=true`，由 `ReleaseFormat()` 内部再尝试一次清理；
- 其余路径（写入失败、超时、Abort、析构）统一走修复后的 `ReleaseFormat()`。

## 5. 修复后复测证据

| 项目 | 值 |
|---|---|
| 源码归档 | `rkav-rtsp-close-leak-fix-source-20260912-1744.tar.gz`，SHA-256 `8e613afd...1d716` |
| 构建与测试 | Release 原生构建；`ctest` 73/73 通过 |
| 5 分钟探针 | 7 次重连；FD 恒定 4↔5；RSS 从 t+3.5 分钟起稳定在 40880 KB |
| 30 分钟复测 | 退出码 0；`reconnected=43`、`connection_lost=44`、`fatal=0`；`captures=44` |
| RSS 曲线 | 预热后 ~41.0 MB，随后 20 分钟稳定在 40418–40432 KB（±14 KB），无线性增长 |
| FD 曲线 | `fd_min=4`、`fd_max=5`，与连接/断开状态一致，与重连次数无关 |
| 捕获抽检 | `cap-001.mp4` 30.021 秒、`cap-044.mp4` 30.780 秒，均含 H.264+AAC |
| 残留进程 | 无 |
| 证据目录 | 探针 `leakfix-probe-20260912-174554`；长稳 `leakfix-soak-30m-20260912-174554` |

关键哈希：复测 `gateway.log` `54c8f7e2...14399`、`resources.csv` `25364bc3...ebe45`。

### 5.1 2 小时长稳（同一修复版本）

| 项目 | 值 |
|---|---|
| 运行时长 | 7200 秒（18:45:31 → 20:45:31），复用修复后 Release 二进制 |
| 接收端循环 | 每 30 秒捕获 + 10 秒断连，共 176 个周期 |
| 重连 | `reconnected=175`、`connection_lost=175`，全部恢复；`fatal=0` |
| 网关退出码 | 0 |
| RSS | 样本 722 个；预热后 40878 KB → 结束 40896 KB（90 分钟 +18 KB），最大值 40896 KB |
| FD | 全程 4↔5，与连接/断开状态一致 |
| 捕获 | 176 段；`cap-001.mp4` 30.021 秒；`cap-176.mp4` 25.912 秒（被运行结束截断） |
| 残留进程 | 无 |
| 证据目录 | `leakfix-soak-2h-20260912-184531`（约 148 MB，含全部捕获与日志） |
| 关键哈希 | `gateway.log` `97a984a7...34667`、`resources.csv` `04d7d6e4...6d2c3` |

RSS 每 15 分钟均值：38.2 MB（t+14，含预热）→ 40.878 → 40.882 → 40.884 → 40.889 →
40.892 → 40.894 → 40.896 MB（t+120），全程无增长趋势。对照修复前 35 KB/分钟的线性增长，
2 小时将增长约 4.2 MB，而实际仅 +18 KB。

## 6. 边界与未完成项

- 长稳使用 PC Mock 源 + FFmpeg 软件编码 + 本机回环，不代表板端 MPP 编码、跨网段或
  systemd 托管的 2/12 小时验收；板端长稳仍按固定顺序在 MPP 通过后进行。
- UDP 传输下的重连资源收尾未单独长稳，但与 TCP 共用同一会话收尾路径。
- 捕获端（ffmpeg demuxer listen）在 UDP 场景有一次性端口 bind 重试警告，属于接收端行为，
  不影响推流侧结论。

## 7. 涉及名词

- **文件描述符泄漏**：进程持有的 socket/文件句柄未关闭，长时间运行会耗尽上限并导致
  新建连接失败；重连类功能必须每次收尾旧会话。
- **muxer 收尾**：`av_write_trailer` 触发 muxer 的 `write_trailer`（RTSP 为 `rtsp_write_close`），
  释放协议内部资源；`avformat_free_context` 不替代它。
- **长稳判据**：RSS 不随重连次数线性增长、FD 有界、零致命错误、捕获完整。

## 8. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [RTSP 断连恢复实现与 PC 端到端验证交接](69-RTSP断连恢复实现与PC端到端验证交接.md)
- [RTSP 传输选项与超时守卫实现及 PC 验证交接](70-RTSP传输选项与超时守卫实现及PC验证交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md) 与 [通俗版](07-项目问题汇总-通俗版.md)，P119
