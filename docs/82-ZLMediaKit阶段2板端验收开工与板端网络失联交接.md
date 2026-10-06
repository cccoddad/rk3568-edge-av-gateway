# ZLMediaKit 阶段 2 板端验收开工与板端网络失联交接

更新日期：2026-09-27
状态：**已归档（P129 历史交接）——失联处置与阶段 2 验收结果见
[docs/83](83-ZLMediaKit阶段2验收全项通过与srtp缺陷修复交接.md)（最新交接，
a-g 七项全通过、P131 srtp 修复、P132 第四次失联待观察）。本文件保留 P129
现场与验收清单原文。**

## 1. 本会话完成内容

### 1.1 路线决策：5.10/DTS 升级与 p1 还原全部关闭（P128，已提交 d336446）

1. **原厂包可得性终查**：网络搜索无有效命中；档案 docs/32 已核验的三处公开渠道
   （LubanCat 官方 6.63 GB Full SDK 归档 0 个 GEC/V11 命中、LubanCat device_rockchip
   无 GEC 配置、GecEdu 公开仓无任何固件镜像）+ 用户向粤嵌索取不可得——**结论：公开与
   厂商渠道均无 GEC V11 原厂 uboot.img/刷机包**。
2. **"是否必须升级"的证据对账**：
   - 升级原始动机（2026-09-02 MPP 配置阶段返回 -1，疑版本不匹配）**已被证伪**：
     真实根因为配置键拼写 `rc:fps_*_denom` vs 旧运行库只认的历史拼写 `denorm`（P115，
     docs/67），修复后 2026-09-14 在 **4.19 上**板端 MPP 10 秒短测（P122）与 RTSP
     推拉/断连恢复（P123）全部通过——"必须换内核"的技术理由消失；
   - 后续业务步骤逐项对账：ZLMediaKit 阶段 2（ZLM 已部署在 4.19 板上运行）、真实 OSD、
     2/12 小时长稳、USB 热重连——**全部不依赖 5.10**；屏/触摸/WiFi 在原厂 4.19 下本就
     工作且业务链路不用本地屏；
   - systemd 在 4.19 与 5.10 两条路上都不存在（板端为 Buildroot + SysV init，
     zboot/DTS 候选只换内核+DTB 不换 rootfs），维持"PC 验证 + 板端 SysV 偏差记录"口径。
3. **用户决策**：不升级、不刷写；p1 保持 9/16 新 U-Boot（还原动机=misc 破坏，已修复
   读回 `eb0ffa…`；当前 U-Boot 实测可引导 4.19；再刷纯增风险）；docs/80 候选封存归档。
4. **归档落点**：docs/81 §0/§7、docs/80 状态头、docs/19 §1+§7 第 0 条、P128 入
   docs/06（含"两次真机启动白做了吗"的追问答法）与 docs/07（含故事线第 36 条）。
   提交 `d336446` → origin/main，`git diff --check` 通过。

### 1.2 ZLMediaKit 阶段 2 板端验收开工（准备完成，验收未开始）

板端只读核验全部通过：

| 项 | 结果 |
|---|---|
| 系统 | Linux 4.19.232，uptime 4:34，load 0.16 |
| ZLM 进程 | MediaServer PID 865/867 守护运行（`/proc/*/cmdline` 扫描确认） |
| 监听端口 | 1935(RTMP)、8080(HTTP)、8000(WebRTC)、8554(RTSP) 全部 LISTEN |
| API（板端 localhost） | `getMediaList` code:0；`getThreadsLoad` code:0（4 event poller，load 0） |
| secret | `y4YVy5XjFCAffNR2h14glk20XxexRw8u`（conf 已核对） |
| 网关 | ELF `90d025b8…` 82,588,608 字节在 `/userdata/rkav/mpp-rtsp-board-20260914-1530-4242/` |
| 推流配置 | `rk3568-rknn-mjpeg-alsa-mpp-rtsp.json`（rtsp required/tcp/重连 1000ms）+ 运行脚本模板 |
| 摄像头 | `/dev/video9` = UGREEN Camera 2K |
| 麦克风 | ALSA card 2 = U2K（hw:2,0） |
| 模型 | `yolov5s-rk3568.rknn` 在位；userdata 剩余 7.3G |
| WebRTC 页 | `www/webrtc/index.html` 文件在；但 `http://127.0.0.1:8080/webrtc/index.html`
|  | 返回 404——**正确 URL 待核**（阶段 2 验收项之一） |

### 1.3 板端网络整机失联（P129，未定位）

时序（2026-09-27 会话内）：

1. 宿主机 `curl http://192.168.50.2:8080/…` 首次无输出，重试 8 秒超时
   （注意：此前三次 adb shell wget **板端 localhost API 一直正常**——应用层健康）；
2. 随后 adb `device offline` → `connect` 超时；`ping 192.168.50.2` 100% 丢包；
3. 宿主侧复核：网卡 192.168.50.1/24 正常、CH340 串口 COM9 在位；
4. 会话末复测：ping 3/3 丢、adb connect 1060 超时——**板子仍全失联**；
5. **证据缺口：失联瞬间串口监听未武装**（serial_watch 启动尝试被中断），无任何板端
   日志——这是本会话最大的方法学教训（见 §3）。

## 2. 证据说明什么

- **分层缩小故障域**：板端 localhost API code:0（ZLM 应用健康）→ 宿主 TCP 8080 超时
  （网络路径断）→ ping 100% + adb 离线（整机网络/主机死）——失联发生在**第 2 层到第 3
  层之间**，与 ZLM 进程本身无关；
- 网关/摄像头/麦克风/模型/推流配置全部预核验在位——网络一恢复，阶段 2 可直接从
  "推流 30 秒"开始，无需重新准备；
- 路线关闭决策（§1.1）已提交推送，后续会话读 docs/81 §7 即得完整因果链，不会再被
  "计划惯性"带偏。

## 3. 问题归档

| 编号 | 主题 | 状态 |
|---|---|---|
| P128 | 升级动机被证伪后的路线决策：关闭 5.10/DTS 与 p1 还原，回到 4.19 收口 | 已决策归档（d336446） |
| P129 | 板端网络在阶段 2 验收准备中整机失联：分层定位 + 串口预武装教训 | **未定位，下一步先串口** |

另有两条**工具链备忘**（不入 P 编号，非项目问题）：

1. 本会话曾把中转站日志的 GLM-5.3 归因于"渠道故障回退"，经核为误判——真相是
   `settings.json` 的 `ANTHROPIC_MODEL=glm-5.3` 使自动模式安全分类器（system prompt
   为 "You are a security monitor for autonomous AI coding agents"）一直请求 glm-5.3；
   已修复为 mimo-v2.6-flash（备份 `settings.json.bak-mimo-default`）。教训：**归因前先
   分清请求来源，环境提示行（powered by the model …）才是本会话真身**。
2. CC Switch 保存的供应商模板可能存有旧 glm 默认值，下次切换供应商后要复查
   `~/.claude/settings.json`。

## 4. 下一步操作（按序）

1. **串口取证（先做，只读）**：`powershell -NoProfile -ExecutionPolicy Bypass -File
   D:\share\serial_watch.ps1 15 D:\share\serial-netloss-20260927.txt` 后台武装 15 分钟，
   观察板子是否已自行恢复输出、是否有网卡 watchdog/OOM/panic 痕迹；
2. **断电重启（需用户，按 81 §8 纪律）**：拔全部线含黑色电源线 15 秒插回（此板禁插
   TF 卡）→ 串口全程盯启动 → 若网口恢复则先补一份 dmesg 证据；
3. **只读定位失联根因**：`dmesg | grep -iE 'eth|watchdog|oom|err'`、`/sys/class/net/eth0/
   carrier`、对比 uptime 判断是否重启过、ZLM 是否仍在（/userdata 持久）——先取证再结论；
4. **ZLM 阶段 2 验收清单**（网络恢复后，每项唯一证据目录 + dmesg before/after + SHA-256）：
   - a. 宿主→板 8080 可达性（顺带排查宿主防火墙出站规则）；
   - b. 网关推流 30 秒（复用 1530-4242 模板，新目录 `zlm-stage2-push-<ts>`）；
   - c. 3 路并发 RTSP 拉流（宿主 ffmpeg ×3）+ `getMediaList` readers=3；
   - d. HTTP-FLV / RTMP / HLS 各拉 5 秒；
   - e. WebRTC 浏览器播放（先核正确页面 URL，www/webrtc 文件在但直链 404）；
   - f. 端到端延迟测量（画面时钟方案）；
   - g. 板端 CPU/内存（top 快照 + getThreadsLoad）。
   参照数字全部来自 PC 阶段 1（docs/73：CPU 5.6%/内存 8.5 MB、3 路 3600 秒无缺口），
   简历只写板端实测数字。
5. **其后固定顺序不变**（docs/19 §7）：真实 OSD 板端验证 → 2/12 小时长稳（板端 SysV
   托管偏差照实记录，systemd 单元仅 PC 证据口径）。

## 5. 名词

- **分层缩小故障域**：按"应用层 localhost → 网络路径 → 整机"顺序逐层测，用每层的
  通过/失败把故障挤进最小范围；
- **串口预武装**：网络实验开始**之前**就启动串口监听——故障发生后再想抓日志，
  证据已经永远丢失；
- **busybox ps 局限**：板端 `ps w` 只列出当前 shell 视角的少量进程，完整进程要用
  `/proc/[pid]/cmdline` 扫描；
- **守护进程 `-d`**：MediaServer fork 到后台继承会话描述符，adb 会话会"卡住"（P124），
  测完 API 用 `wget` 直接查，别等进程退出。

## 6. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [GEC V11 白屏变砖串口诊断与 419 原厂系统恢复交接](81-GEC-V11白屏变砖串口诊断与419原厂系统恢复交接.md)（救援纪律与工具）
- [板端 RTSP 推流与断连恢复及 FFmpeg 交叉构建交接](77-板端RTSP推流与断连恢复及FFmpeg交叉构建交接.md)（推流测试模板与板端 ZLM 部署记录）
- [ZLMediaKit 阶段 1 PC 验收交接](73-ZLMediaKit阶段1PC验证交接.md)（验收方法与参照数字）
- [ZLMediaKit aarch64 交叉构建与部署包交接](74-ZLMediaKit-aarch64交叉构建与部署包交接.md)（部署包与端口策略）
- [GEC V11 B 方案 DTS 移植候选构建与门禁交接](80-GEC-V11-B方案DTS移植候选构建与门禁交接.md)（已封存）
- 问题台账，P128/P129
