# OSD 板端冒烟通过与长稳进行中交接（docs/84）

## 0. 给下一段对话的提示词（直接复制给新会话）

> 你在维护 RK3568 实时音视频边缘分析网关（RKAV）。先读 `AGENTS.md`、`docs/19-项目当前开发状态.md`
> 和本文件 §1-§3，再按固定路线推进。当前时间点 2026-09-29 晚：
>
> 1. **真实 OSD 板端冒烟已通过**（P133 修复二进制 `/userdata/rkav/osd-flagfix-20260928/rkav-gateway`，
>    exit=0、overlay 475/475、errors 0、dmesg 零新增、抽帧目视通过），已提交 `01bd8ae` 并推送。
> 2. **2 小时长稳已通过**（2026-09-29 23:24 验收）：exit=0、overlay 27187/27187、
>    skipped 0、errors 0、dmesg 前后 SHA 一致；证据 `osd-soak-20260929-131338` +
>    PC `D:\share\osd-soak-2h-20260929-131338`（已 sha256 校验）。
> 3. **12 小时长稳运行中**：板端目录 `osd-soak-20260929-155451`（板端 15:54 起跑，
>    duration 43200，约本地明早 11:54 结束）。验收命令：
>    `adb -s 192.168.50.2:5555 shell "cat /userdata/rkav/osd-soak-20260929-155451/result.txt"`
>    —— exit_code=0 且 skipped/errors 均为 0 即通过；通过后拉证据、更新 docs、进入收口拍板。
>    **过夜期间板卡严禁断电/重启**（RTC 无设备节点，冷启动回 1970；重启=拔全部线含黑色电源线，
>    本板永久禁插 TF 卡、禁刷 p1/p3）。
> 4. 12h 通过后进入 **systemd/SysV 收口**（差异清单见 §3：板端 Buildroot 2018.02 **无 systemd**、
>    `/etc/init.d` 无 rkav 条目、无 rkav 用户、`/opt/rkav` 下无网关二进制——收口方案需先与用户确认，
>    未经确认不得改板端启动方式）。
> 5. 固定路线其余顺序不变：收口 → ZLM 遗留（P124 等）→ P132 断连观察。
>
> 硬性协作规则（每轮不可省略）：操作后四段式汇报（做了什么/证据说明什么/成败/名词解释）；
> **输出分块纪律**：单条回复保持短篇幅，长文档只许分段 Edit 追加、禁止整篇 Write，
> 日志只引用决定性行——这是防 `64000 output token maximum` 撞顶的主防线（详见项目记忆
> `avoid-64k-output-overflow`）；git 只提交已验证单元，`.clauderc`、`docs/65(项目全面分析)`、
> `docs/66`、`tools/tftp_receive.py`、`tools/docker/rknn-gateway-build.Dockerfile`、`.cursor/rules/*`
> 保持不提交；push 走 `git -c http.proxy=http://127.0.0.1:7897 -c https.proxy=http://127.0.0.1:7897 push origin main`，
> 禁止 force push；失败后先存日志与 SHA-256，不盲目重跑。

## 1. 本会话完成内容

### 1.1 AI 输出超限问题系统性定位（工具侧，非项目 P 号）

- 从 `claude.exe` 二进制提取触发逻辑：API 返回 `stop_reason === "max_tokens"`
  （单次响应生成到请求的 `max_tokens` 上限）时 Claude Code 直接抛
  `exceeded the <N> output token maximum`，**不自动续跑**；报错中的 N = 生效的
  `CLAUDE_CODE_MAX_OUTPUT_TOKENS`。历史会话在 32000（4 次）与 64000（多次）两档均被撞破。
- 落地措施：主防线是**输出分块纪律**（长文档分段 Edit 追加、单条回复短小、日志只引关键行），
  写入项目记忆 `avoid-64k-output-overflow` 与 `rkav-model-proxy-config`（每会话自动加载）；
  全局 settings 中 mimo `effortLevel` 由 xhigh 改为 high（若被运行中会话写回，UI 切档或会话后重设）。
- 全项目通读（8 子系统 Workflow 并行 + 批判审查）：`.clauderc` 确认为惰性文件，
  真实配置在 `C:\Users\CC\.claude\settings.json`（端口 3456）；风险清单确认
  docs/19/06/07 同步更新义务是结构性大输出来源，必须分段 Edit。

### 1.2 真实 OSD 板端冒烟验证（固定路线当前单元，已通过）

- 板卡在线 `192.168.50.2:5555`（网络 adb），起跑前空闲（无网关、load 0.16）。
- `board_osd_run.sh smoke` 120 秒：exit=0、`overlay_applied_total=475`、skipped 0、
  `errors_total=0`、推理 476/476、视频 1872 帧、sink 7498/7498 零丢包、
  `dmesg.before/after` SHA-256 完全一致（内核零新增）。
- 证据拉回 PC 且 `sha256sum -c` 全通过；抽帧（10s/60s）目视确认：真实画面 + 检测框 +
  `person 0.94`/`cell phone 0.59` 置信度 + 四角水印/时间戳/帧计数全数渲染。
- 哈希板端与本地一致（smoke `0ea43c13…`、longrun `91db3751…`、脚本 `2cfd632b…`）。
- 已提交 `01bd8ae`：2 份 OSD 配置 + `tools/board_osd_run.sh` + docs/19 两处状态更新，
  经 clash 7897 推送（首次 504 重试即过）；须保持不提交的脏文件全部未动。

## 2. 证据索引

| 项 | 位置 / 值 |
|---|---|
| 板端冒烟证据目录 | `/userdata/rkav/osd-smoke-20260929-121803` |
| PC 镜像目录（11 文件已 sha256 校验） | `D:\share\osd-smoke-board-20260929-121803` |
| 关键结果 | `result.txt`：exit_code=0，applied=475，skipped=0，errors=0 |
| 停止指标 | application_stopped：inference 476/476、video 1872、各队列零丢、recoveries=0 |
| dmesg 前后 | SHA 同为 `310b4dae…`（零内核新增） |
| 目视抽帧 | `frame_10.png`、`frame_60.png`（检测框+置信度+水印+时间戳+帧计数） |
| 含修复二进制 | `/userdata/rkav/osd-flagfix-20260928/rkav-gateway`（SHA `de833c68…`） |
| 2h 长稳（已通过 2026-09-29） | exit=0、overlay 27187/27187、errors 0、dmesg 零新增；板端 `osd-soak-20260929-131338`、PC `D:\share\osd-soak-2h-20260929-131338`（sha256 校验通过） |
| 12h 长稳（运行中） | `osd-soak-20260929-155451`（板端 15:54 起，duration 43200，约本地 11:54 结束），validate 通过、zlm=running |
| 本单元提交 | `01bd8ae`（origin/main 已同步） |

## 3. 当前状态、收口差异清单与下一步

**进度**：2 小时长稳已通过（2026-09-29 23:24，exit=0、27187/27187、dmesg 零新增）；
12 小时长稳运行中（`osd-soak-20260929-155451`，RTSP-only，日志估算 ~72MB，磁盘充足）。
12h 通过 → 进入收口拍板（docs/85 §4 三个决策点需用户确认）。

**systemd/自启收口差异清单（2026-09-29 板端实测，收口方案需用户确认后执行）**：

1. 板端为 **Buildroot 2018.02-rc3，无 systemd**（`pidof systemd` 为空）——
   `deploy/rkav-gateway.service` 无法直接部署；候选方案为 BusyBox SysV `S99rkav` 脚本
   或引入 systemd（重量级，须用户决策）。
2. `/etc/init.d` **无任何 rkav 自启条目**——网关现为手动拉起（本会话即 nohup 方式）。
3. 无 `rkav` 用户/组（`/etc/passwd` 无记录）——service 文件 `User=rkav` 不成立。
4. `/opt/rkav` 下只有 `zlm`，无 `bin/`、`etc/`——service 文件 `ExecStart=/opt/rkav/bin/rkav-gateway`
   路径不存在；网关二进制分散在 `/userdata/rkav/<版本目录>/`。
5. `/var/lib/rkav` 不存在——service `ReadWritePaths=/var/lib/rkav /tmp` 不覆盖
   实际读写的 `/userdata/rkav`（配置/模型/证据）。
6. `ExecStart` 默认配置 `rk3568-rknn-mjpeg-alsa.json` 与 OSD 路线配置不同。
7. `StandardOutput=journal` 依赖 journald，板端无 journal——日志方案需改 file/syslog。
8. 已验证的有利面：service 文件 `systemd-analyze verify` 曾退出码 0（PC/容器侧），
   SIGTERM/StartLimit 等键位已按规范修正（docs/19 §6 行 systemd 服务文件优化与校验）。

**下一步（固定顺序，不得跳跃）**：2h 验收 → 12h 长稳 → 收口（先出方案与用户确认）
→ ZLM 遗留（P124 等）→ P132 断连观察。**当前挂起 P 号**：P132（板端第 4 次失联，观察中）、
P124（ZLM 阶段遗留）、P133（本会话板端冒烟已过，待 12h 后可标全闭环）。
