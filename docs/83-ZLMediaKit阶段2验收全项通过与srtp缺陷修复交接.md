# ZLMediaKit 阶段 2 板端验收全项通过与 srtp 缺陷修复交接

更新日期：2026-09-28
状态：**ZLM 阶段 2 验收 a-g 七项全部通过（e 项经 libsrtp 重建修复）；P129/P130 失联
系列已处置，第四次失联独立归档 P132 待观察；本文件为最新交接。**

## 1. 本会话完成内容

### 1.1 P129 处置：串口取证与断电恢复（2026-09-27）

1. 按 docs/82 §4 武装串口后发现 COM9 打不开，逐层定位：PnP OK → 原始
   `CreateFile` 独占成功 → **`SetCommState` Win32 ERROR_GEN_FAILURE(31)** →
   裸读 5 秒 0 字节。结论：**CH340 枚举在但芯片不应答 = 板端 hang 的宿主侧指纹**
   （诊断脚本 `D:\share\probe_com9*.ps1` 留档）。
2. 按 81 §8 真断电重启，串口全程抓到 U-Boot → 内核 → root（4.19.232、uptime 归零），
   ping 4/4、adb 恢复；dmesg 无 watchdog/panic，仅历史已知噪声。
3. 板端时间 1970，`date -u` 校时后起 ZLM（**每次重启必须校时**，否则 DTLS 证书日期异常）。

### 1.2 ZLM 阶段 2 验收 a-d、g（2026-09-27）

| 项 | 结果与证据 |
|---|---|
| a 宿主→板 8080 | HTTP 200、13ms；板端 API code:0 |
| b 30 秒推流 | `zlm-stage2-push-20260927-143809`：exit 0，861 视频帧/1500 音频块/推理 145，errors 0，sink_rtsp、sink_h264 各 2230/2230 dropped 0，H.264 10,213,765B（SHA `39753edd…`），dmesg 增量仅 USB audio freq 观察项 |
| c 3 路并发 RTSP | `zlm-stage2-3way-20260927-152330`：`getMediaList` 并发期 **readerCount=3**；宿主 3×ffmpeg 各录 10.01 秒（各 3.36MB，H.264 1280x720+AAC 48k），解码零错误；网关 60 秒 exit 0、sink 4459/4459 |
| d FLV/RTMP/HLS | RTMP 1,723,129B、HLS 1,714,956B、**HTTP-FLV 1,746,883B**（正确 URL `/live/camera.live.flv`，`.flv` 后缀是猜错的）；各拉 5 秒 exit 0 |
| g 板端资源 | 负载中采样：load 0.87、内存 58.6MB/2.0GB、**ZLM RSS 16.4MB**（PC 参照 16.2MB 同量级）、4 event poller load 0 |

另：f 项端到端延迟以**同源时钟法**完成（宿主 `local-clock.html` 大字时钟被摄像头拍摄，
消除首测"网络授时 vs 宿主钟"偏差）：三帧测得延迟 **0.2~1.1 秒**，上界含单次连接起播
开销，**稳态约 0.3~0.7 秒**，口径"≤1.1 秒（含抓帧工具开销）"，证据
`zlm-stage2-e2eplay-20260927-215945`。首测（授时网页）因时钟源偏差与待测量同量级判废。

### 1.3 e 项黑屏根因与 srtp 缺陷修复（P131，2026-09-28）

现象与分层定位（每层证据）：

1. 浏览器 vConsole：信令成功、状态到 `connected`，但无画面（`-400 offer empty` 是
   地址栏直接 GET API 的预期报错，与播放无关）；
2. 自制诊断页 `zlm-play.html`（已推到板端 www，`http://192.168.50.2:8080/zlm-play.html`）
   显示：**DTLS connected（含 cipher）✓、ICE ✓、但 inbound-rtp packetsReceived = -1（零收包）**；
3. 服务端日志：DTLS `handshake done` ✓、服务器持续发 RTP ✓ —— 四层通却无媒体；
4. **真凶**：`chosen SRTP crypto suite: SRTP_AEAD_AES_256_GCM` 后紧跟
   **`srtp_create() failed: unspecified failure`** ——服务器 libsrtp 创建 SRTP 会话失败，
   一个媒体包都发不出。
5. 根因链：docs/74 构建 `libsrtp ./configure --host=aarch64-linux-gnu` **未启用
   OpenSSL 引擎** → 无 GCM → Chromium 首选 AEAD_AES_256_GCM → 服务端自选了自己
   不支持的套件 → 黑屏。RTSP 路径（人脸考勤控制台同时在拉同一路流且正常）不经 SRTP，
   故不受影响——它反而证明了"摄像头→网关→编码→ZLM→读者"链路健康。

修复（用户决策"重建"）：

- **重建脚本**：`D:\share\vm_zlm_srtpfix_build.sh`（VM 内执行
  `bash /mnt/hgfs/share/vm_zlm_srtpfix_build.sh`，日志 `vm_zlm_srtpfix_build.log`）；
  关键改动：libsrtp `--enable-openssl --with-openssl-dir=$PREFIX`（守卫
  `leverage OpenSSL crypto... yes`，否则失败退出）、清 ZLM 构建树全量重编
  （ENABLE_PLAYER=ON）、`docker run -v WORK:/work -v WORK/prefix:/opt/zlm-deps`；
- **产物**：`rkav-zlmediakit-aarch64-srtpfix-20260928-151435.tar.gz`（14.4MB，
  SHA-256 `7283ac614b464a18209dd3944627b6f9650f6801ac068156bb07733c9a9cf931`，
  证据目录 `D:\share\zlm-srtpfix-build-20260928-151435/` 含全套构建日志与 BUILD_INFO；
  门禁 GLIBC 最高 2.34、NEEDED 与原包一致）；
- **板端部署**：`gunzip -c … | tar x`（BusyBox tar 无 -z），只换 `bin/MediaServer` 与
  `lib/*.so*`（软链完好），**保留板端 conf（rootPath/端口/secret 修改）与 www（含
  zlm-play.html、webrtc 页）**；旧二进制备份 `bin/MediaServer.bak-noopenssl`；
  新 MediaServer SHA-256 前缀 `16e2c47e…`；`date -u` 校时后起服；
- **复测**：重起推流后浏览器播放页出画面（用户截图实证）——**e 项通过**。

### 1.4 验收总表（阶段 2 全绿）

a ✓ / b ✓ / c ✓ / d ✓ / **e ✓（srtpfix 后出画面）** / **f ✓（≤1.1 秒）** / g ✓ —— 详 §1.2、§1.3。

### 1.5 第四次失联（P132，2026-09-28）

- 现象：ping 100%、adb 离线；**CH340 PnP OK、串口可打开但发回车零响应**；
  （第三次失联 9/27 20:54 用户确认"电源灯一直亮"）——供电在、SoC 无响应 = **静默
  hang 态**，与 P130 电源短路（芯片不应答 SetCommState）指纹不同；
- 期间完成串口交互探针 `D:\share\serial_probe_alive.ps1`（发 CR 读回显判 shell 活性；
  注意 PS5.1 对无 BOM UTF-8 中文注释按 GBK 误读会破坏语法，脚本注释须用 ASCII）；
- 用户真断电重启后恢复（串口全程抓到），dmesg 无 panic/oops/watchdog 残留；
- **定性**：电源短路 9/27 已由用户排除，本次是独立的复发性问题 → 归档 P132 待观察。

## 2. 证据说明什么

1. **分层定位方法再次闭环**：浏览器统计页（零收包）→ 服务端 DTLS 日志（握手成功）
   → 服务端 SRTP 错误行（srtp_create fail）→ 构建脚本参数（缺 --enable-openssl），
   四层证据互相印证，最终修复由"浏览器出画面"这个端到端事实验证；
2. **构建参数是嵌入式验收的隐性风险面**：GLIBC/NEEDED 门禁全过 ≠ 功能完整，
   srtp 引擎这种"编译期开关"只有真浏览器实测才暴露（阶段 1"服务端就绪"边界预判正确）；
3. a-g 全绿 = ZLM 流媒体服务层板端验收完成，简历口径可写实测数字（7 项、
   readerCount=3、三协议各 5 秒、延迟 ≤1.1 秒、ZLM RSS 16.4MB）；
4. 失联系列：P129（首发，串口未武装）→ P130（复发+串口取证+电源短路确认）
   → P132（第四次，供电正常下静默 hang，独立待查）——每次失联的宿主侧指纹
   （SetCommState 31 / CH340 消失 / 能开但无响应）是快速判型的关键。

## 3. 问题归档

| 编号 | 主题 | 状态 |
|---|---|---|
| P130 | 板端复发性失联：串口取证、断电恢复、电源短路确认 | 已处置（用户排除短路）；复发部分转 P132 |
| P131 | ZLM aarch64 构建 libsrtp 缺 OpenSSL/GCM 引擎致 WebRTC 黑屏 | **已解决**（重建+部署+浏览器复测出画面） |
| P132 | 第四次失联：供电灯亮下的静默 hang（独立于电源短路） | **待观察**：复发时先串口取证再断电 |

## 4. 下一步操作（按序）

1. **真实 OSD 板端验证**（固定路线第一项，docs/19 §7）：CPU OSD 目前只有 PC 软件
   MP4 证据，需在 RK3568 板端验证检测框/文字叠加后的真实输出；
2. **2/12 小时长稳**（板端 SysV 托管偏差照实记录，systemd 单元仅 PC 口径）；
3. systemd/SIGTERM/日志轮转收口；
4. ZLM 侧遗留（非阻塞）：板端 ZLM 未纳入 init（P124）、`www/webassist` 播放页与
   RTSP 人脸控制台联动可作加分项；板端时间依赖手工 `date -u`（RTC 坏）建议
   ntpd/开机校时纳入偏差记录；
5. P132 监控：串口监听在失联时的第一动作仍是武装取证（`serial_watch.ps1`）。

## 5. 名词

- **SRTP_AEAD_AES_256_GCM**：DTLS-SRTP 协商出的媒体加密套件；libsrtp 需编译期
  `--enable-openssl` 才支持，缺失时 `srtp_create` 直接失败；
- **rootPath 解析基准**：ZLM HTTP 静态根按**可执行文件目录**（bin/）解析相对路径，
  部署包把 www 放在包根——因此曾 404，改绝对路径 `rootPath=/opt/rkav/zlm/www` 修复；
- **串口交互探针**：向串口发 CR 读回显，区分"内核活着的 shell"与"芯片独立应答"；
- **PS5.1 编码坑**：无 BOM UTF-8 脚本含中文注释会被按 GBK 解析致语法破碎，注释用 ASCII。

## 6. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [ZLMediaKit 阶段 2 板端验收开工与板端网络失联交接](82-ZLMediaKit阶段2板端验收开工与板端网络失联交接.md)（P129 历史）
- [GEC V11 白屏变砖串口诊断与 4.19 原厂系统恢复交接](81-GEC-V11白屏变砖串口诊断与419原厂系统恢复交接.md)（救援纪律）
- [ZLMediaKit aarch64 交叉构建与部署包交接](74-ZLMediaKit-aarch64交叉构建与部署包交接.md)（原构建参数出处）
- [ZLMediaKit 阶段 1 PC 验证交接](73-ZLMediaKit阶段1PC验证交接.md)（参照数字）
- [板端 RTSP 推流与断连恢复及 FFmpeg 交叉构建交接](77-板端RTSP推流与断连恢复及FFmpeg交叉构建交接.md)
- 问题台账，P130/P131/P132
