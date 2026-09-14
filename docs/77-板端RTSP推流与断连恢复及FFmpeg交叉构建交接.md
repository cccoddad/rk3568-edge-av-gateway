# 板端 RTSP 推流与断连恢复及 FFmpeg 交叉构建交接

更新日期：2026-09-14
状态：**板端 RTSP 实机验证通过（推流→ZLMediaKit→拉流，含服务器重启断连恢复）；板端网关已具备
FFmpeg 静态后端；ZLM aarch64 已部署到板端（阶段 2 部署执行、多客户端/WebRTC/延迟验收待做）**。
本文件是当前最新会话交接。

## 1. 一句话结论

在完成 MPP/RGA 板端短测（docs/76）后，本轮把固定路线的"RTSP 与恢复"推到板端实机通过：

- 板端网关用 **MPP 硬编码 H.264 + FFmpeg AAC** 推流到板端 **ZLMediaKit**（`rtsp://127.0.0.1:8554/live/camera`），
  Ubuntu VM 从 `rtsp://192.168.50.2:8554/live/camera` 拉流：`ffprobe` 得到 H.264 1280x720 +
  AAC 48 kHz 立体声，录制 10 秒 262 帧，全量解码零错误；
- **断连恢复**：运行中杀掉 ZLM，网关以 1 秒间隔重试（10 次可重试错误、零致命错误），ZLM 重启后
  **1.5 秒内** 自动重建 RTSP 会话（`rtsp_reconnected`），重启后拉流 8 秒 209 帧、解码零错误；
- 两次测试期间 `dmesg` 与基线逐字节一致（零新增内核日志），网关退出码 0、零恢复、零队列丢弃
  （RTSP/sink 队列），H.264 证据文件 9.7 MB / 19.5 MB；
- 为达成本轮目标完成了 **FFmpeg 6.1.1 aarch64 静态交叉构建**并纳入构建脚本；板端网关 ELF 82.6 MB、
  最高 GLIBC 2.35（等于板端上限）、NEEDED 无 `libav*.so`（全部静态入包）。

## 2. 本轮新增/修改的仓库内容

| 文件 | 变更 |
|---|---|
| `tools/build_ffmpeg_aarch64_container.sh` | 新增：在容器内用 Gitee 镜像构建 FFmpeg 6.1.1 静态 aarch64 前缀（一次性、可复现） |
| `tools/build_rknn_gateway.sh` | 新增可选 `RKAV_FFMPEG_PREFIX`：设置 `PKG_CONFIG_PATH`/`PKG_CONFIG_ARGN=--static` 并传 `-DRKAV_WITH_FFMPEG=ON` 与 `-L` 链接目录 |
| `tools/build_rknn_gateway_container.sh` | 新增可选 `RKAV_FFMPEG_PREFIX`（挂载 `/opt/ffmpeg`）与 `RKAV_RKNN_BUILD_DOCKERFILE`（镜像 Dockerfile 覆盖） |
| `tools/docker/rknn-gateway-build-ffmpeg.Dockerfile` | 新增：在既有构建镜像上补装 `pkg-config`（不改用户的原 Dockerfile） |
| `src/output/ffmpeg/ffmpeg_rtsp_sink.cpp` | 修复：未登记音频流（音频为 checksum 等非 AAC）时忽略音频包而不是报 codec 变化错误（P123） |

## 3. 板端部署与测试步骤（可复现）

1. **FFmpeg 前缀**（VM `rkav-ffmpeg-20260914` 容器，约 30 分钟完成）：
   `docker run ... sh /work/build.sh`，产物 `/home/china/rk3568-work/ffmpeg-build-20260914/prefix`
   （avformat 60.16.100 / avcodec 60.31.102 / avutil 58.29.100 + swscale/swresample；
   `.pc` 前缀已按容器挂载点改为 `/opt/ffmpeg`）。
2. **网关构建**：在容器构建脚本上追加
   `RKAV_RKNN_BUILD_IMAGE=rkav/aarch64-rknn-build:ubuntu22.04-ffmpeg`、
   `RKAV_RKNN_BUILD_DOCKERFILE=<source>/tools/docker/rknn-gateway-build-ffmpeg.Dockerfile`、
   `RKAV_FFMPEG_PREFIX=<ffmpeg prefix>`；最终 ELF SHA-256
   `90d025b80a22c05eb8fbd87cb7f156aab05c88eef90400cdea6116a43b267b88`（82,588,608 字节）。
   源码归档 `rkav-rtsp-ffmpeg4-source-20260914-1524.tar.gz` SHA-256
   `4a226b09ebac94a2ebc188c3390b8422cfe435e5354aace3436a28ea75d03023`。
3. **ZLM 部署**（板端）：推送 `rkav-zlmediakit-aarch64-20260913-232946.tar.gz`（SHA-256
   `7024938012d1c362f9e1c1610444a6b20c541eef960c616a743443396cc1a11c`），解包到
   `/opt/rkav/zlm`；`conf/config.ini` 改 `[rtsp] port=8554`、`[http] port=8080`；以
   `LD_LIBRARY_PATH=$PWD/lib ./bin/MediaServer -d -c conf/config.ini -l 1` 守护模式启动。
   首次启动把 `api.secret` 自动改成 `y4YVy5XjFCAffNR2h14glk20XxexRw8u`（P120 同款行为），
   管理接口需使用新 secret。
4. **推流配置**（板端）：`audio_encoder` 改为 `ffmpeg/aac`；输出 = h264 证据文件 + RTSP
   （`reconnect_interval_ms=1000`、`rtsp_transport=tcp`、`rtsp_timeout_ms=5000`、`required=true`）。
5. **验证**：adb 执行 30 秒 / 60 秒运行脚本，VM 侧并发 `ffprobe`/`ffmpeg -c copy` 拉流录制；
   断连场景在运行中 `killall MediaServer`，10 秒后守护模式重启。

## 4. 结果与证据

### 4.1 RTSP 推拉 30 秒（目录 `mpp-rtsp-board-20260914-1530-4242`）

- 网关 `exit_code=0`、`reason=run_duration_elapsed`；H.264 文件 9,749,816 字节。
- 计数：视频采集 861 / 编码包 784，音频块 1499 / AAC 包 1407，路由=消费 4382；`errors_total 0`、
  `recoveries_total 0`；sink_h264 与 sink_rtsp 各 pushed=popped=2191、**dropped 0**；
  `video_encode` 队列按策略丢弃 77 帧（编码吞吐约 26 fps 对采集约 29 fps，属既有 drop_oldest 策略）。
- ZLM 流列表：`originTypeStr=rtsp_push`、`originUrl=rtsp://127.0.0.1:8554/live/camera`、
  H264 1280x720 30 fps + 音频轨；VM 拉流 `ffprobe` = h264 + aac（48 kHz、立体声），
  10 秒 MP4 3,432,001 字节、262 帧、`ffmpeg -f null -` 零错误。
- `dmesg.before` 与 `dmesg.after` 逐字节相同（73,135 字节）。

### 4.2 断连恢复 60 秒（目录 `mpp-rtsp-reconnect-20260914-1540-7788`）

- 时间线（UTC）：08:02:48 杀 ZLM → 08:02:58 重启 ZLM → **08:02:58.491 `rtsp_reconnected`**。
- 中断期间 10 条可重试错误（`rtsp_connection_lost` + `sink_write_retry`，全部 retryable），
  `errors_total 10`、`recoveries_total 0`，无致命错误。
- 最终计数：视频采集 1721 / 编码包 1561，音频块 2999 / AAC 包 2813；sink 队列零丢弃；
  `video_encode` 队列按策略丢弃 160 帧；H.264 文件 19,467,956 字节。
- VM 拉流：中断前 6 秒 156 帧、重启后 8 秒 209 帧，两段均解码零错误。
- `dmesg.before` 与测试后 `dmesg.postcheck.txt` 逐字节相同（零新增内核日志）。
- 说明：本目录的 `dmesg.after` 与 `exit_code` 因编排脚本卡在 `MediaServer -d` 会话占用而未落盘
  （见 P124）；网关本身按 60 秒时长干净退出（H.264 `.part` 已改为最终文件名 + `application_stopped`）。

### 4.3 归档位置

- 板端：上述两个 `/userdata/rkav/...` 目录；Windows 副本 `D:\share\mpp-rtsp-board-20260914-1530-4242`、
  `D:\share\mpp-rtsp-reconnect-20260914-1540-7788`。
- VM 录制：`D:\share\rtsp-captures-20260914\`（`board-rtsp-capture-10s.mp4`、
  `reconnect-cap1-6s.mp4`、`reconnect-cap2-8s.mp4`）。
- 候选包（WinDownloads）：`rkav-mpp-rtsp-test-20260914-1430-9117`（含最终 ELF、RTSP 配置与脚本）。
- FFmpeg 前缀与构建日志在 VM `/home/china/rk3568-work/ffmpeg-build-20260914/`、
  `rkav-rtsp-ffmpeg4-20260914-1524/container-build.log`。

## 5. 问题归档

| 编号 | 主题 | 状态 |
|---|---|---|
| P123 | RTSP sink 在音频非 AAC 时把未登记音频包当 codec 变化错误 | 已修复，板端验证通过 |
| P124 | 板端 adbd 会杀 shell 后台进程；`MediaServer -d` 占住 adb 会话 | 已定位并给出编排规避方法 |

详见 `docs/06`/`docs/07`。

## 6. 边界与未完成

- 本轮 RTSP 测试为 30/60 秒级；**未做** 2/12 小时长稳、多客户端并发、WebRTC 浏览器播放与端到端延迟测量
  （属 ZLM 阶段 2 验收项）。
- 板端 ZLM 为手工守护进程，**未纳入 init/systemd 托管**；重启后需手工拉起（或后续做 init 脚本）。
- 静态 FFmpeg 使网关 ELF 增至 82.6 MB；GLIBC 上限从 2.34 提高到 **2.35（等于板端）**，余量为零，
  后续如升级工具链需重新做 GLIBC 门禁。
- `video_encode` 队列在 60 秒运行中有策略性丢帧（160/1721）；这是采集 29 fps 对编码约 26 fps 的结果，
  不属本次验收项，但已记录为后续性能优化候选（例如降低采集 fps 或提高编码吞吐）。
- ZLM 管理接口把 AAC 轨显示为 `mpeg4-generic / 8000 Hz / 1ch`，而实际拉流为 48 kHz 立体声；
  属 ZLM 列表接口的显示差异，不影响推拉与解码，后续 WebRTC 验收时再复核。
- 未刷写、未重启板卡、未改板端系统文件（除 `/opt/rkav/zlm` 部署与 ZLM 自身写回 secret）。

## 7. 下一步（固定顺序）

1. **ZLMediaKit 阶段 2 验收**：多客户端并发拉流、HTTP-FLV/RTMP/HLS 在板端可用性、WebRTC 浏览器
   播放（同网段 PC 浏览器）、端到端延迟测量（需画面时钟方案）、板端 CPU/内存占用。
2. **systemd 与长稳**：板端服务化（当前 Buildroot 无 systemd，先用 SysV init 脚本）+
   2/12 小时长稳（与 ZLM 联动）。
3. 其它遗留项按原顺序：USB 进程内热重连、板端 MP4 软件基线（可选）、GEC 5.10 路线。

## 8. 名词

- **ZLMediaKit**：开源流媒体服务器，接收 RTSP 推流后可转 RTSP/RTMP/FLV/HLS/WebRTC 分发。
- **FFmpeg 静态交叉构建**：把 avformat/avcodec 等以 `.a` 形式链入可执行文件，板端无需额外 `.so`。
- **AAC AudioSpecificConfig**：AAC 流的采样率/声道配置，由编码器写入 extradata 供 muxer 使用。
- **守护进程模式 `-d`**：MediaServer 先启动再 fork 到后台；daemon 子进程会继承会话描述符，导致
  在 adb 会话中看似"卡住"（P124）。

## 9. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [板端 MPP 复测与 RGA 映射修复交接](76-板端MPP复测与RGA虚拟地址映射失败交接.md)
- [ZLMediaKit aarch64 交叉构建与部署包交接](74-ZLMediaKit-aarch64交叉构建与部署包交接.md)
- [ZLMediaKit 阶段 1 PC 验证交接](73-ZLMediaKit阶段1PC验证交接.md)
- [RTSP 输出 PC 端到端验证与实现修正交接](68-RTSP输出PC端到端验证与实现修正交接.md)
- [RTSP 断连恢复实现与 PC 端到端验证交接](69-RTSP断连恢复实现与PC端到端验证交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md) 与 [通俗版](07-项目问题汇总-通俗版.md)，P123-P124
