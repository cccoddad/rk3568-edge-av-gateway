# 真实音视频与 RKNN 阶段问题汇总：面试版

更新日期：2026-08-26

## 1. 文档定位

本文记录真实摄像头、麦克风、交叉编译、板端部署、长稳、故障注入和 RKNN 盘点阶段实际遇到
的问题。每个问题按“影响 -> 证据 -> 根因 -> 解决 -> 验证 -> 面试表达”组织。

早期 Mock、并发、Buildroot 和基础网络问题见
[项目问题汇总：面试版](06-项目问题汇总-面试版.md)。面向新手的解释见
[本阶段问题汇总：通俗版](14-真实音视频与RKNN阶段问题汇总-通俗版.md)。

## 2. 问题索引

| 编号 | 问题 | 状态 |
|---|---|---|
| P041 | Windows、Ubuntu 和 RK3568 命令环境混淆 | 已解决 |
| P042 | Windows 文件没有进入 Ubuntu，解压提示不存在 | 已解决 |
| P043 | VMware 拖放不可用，产物无法拖回 Windows | 已解决 |
| P044 | BusyBox 缺少 `httpd`/`nc`，HTTP 传输方案失败 | 已解决 |
| P045 | `eth0` carrier 为 0，板端与 Windows 不通 | 已解决 |
| P046 | TFTP 超时、服务端顺序和 Windows 防火墙 | 已解决，小文件可用 |
| P047 | GitHub 连接失败导致 CMake FetchContent 中断 | 已解决 |
| P048 | `std::byte*` 不能 `static_cast` 为 `int16_t*` | 已解决 |
| P049 | 静态链接 `getaddrinfo` 警告是否代表构建失败 | 已澄清 |
| P050 | 视频节点选错、请求 30 FPS 但实测约 29 FPS | 已解决/已量化 |
| P051 | `arecord --periods` 不受旧版工具支持 | 已解决 |
| P052 | USB Audio `cannot get freq` 警告 | 已评估，继续观察 |
| P053 | ALSA `HW_REFINE` 返回 `EINVAL` | 已解决 |
| P054 | ALSA Capture 已打开但读取链路不正确 | 已解决 |
| P055 | 音频 PTS 不能使用读取完成时刻直接生成 | 已解决 |
| P056 | SIGINT/SIGTERM、定时退出和错误退出混淆 | 已验证 |
| P057 | ALSA `POLLERR` 把断连误判成 XRUN | 已解决 |
| P058 | BusyBox `tar` 不支持 `-z` | 已解决 |
| P059 | 首条长稳资源样本异常低 | 已解释 |
| P060 | SecureCRT 无法输入：流控、会话或内核故障 | 已分类 |
| P061 | 大文件 TFTP 触发网卡 watchdog 和内核异常 | 未修复，已规避 |
| P062 | 多行命令、`>>` 提示符和 `$?` 取值误区 | 已澄清 |
| P063 | RKNN 只有 Runtime 和模型，没有头文件 | 已定位，待接入 |
| P064 | 最新 RKNN SDK/模型不能直接配旧 Runtime | 待按版本闭环 |
| P065 | 板端 MobileNet 与项目目标检测接口不匹配 | 待更换模型 |
| P066 | MJPEG payload 不能直接送入 RKNN | 待实现预处理 |

## 3. 开发与部署问题

### P041：Windows、Ubuntu 和 RK3568 命令环境混淆

**影响：** 路径存在于一台机器，却在另一台机器执行命令，导致“文件不存在”“命令不存在”或
生成错误架构的程序。

**证据：** Windows 文件位于 `C:\Users\CC\Downloads`，Ubuntu 最初却读取
`~/Downloads`；板端提示符为 `root@RK356X:/#`，不具备 CMake 和完整 GNU 工具。

**根因：** 三个系统有独立文件系统、工具链和 CPU 架构。VMware 窗口不是 Windows 终端，串口
终端也不是 Ubuntu shell。

**解决：** 所有操作显式标注环境；Ubuntu 负责 AArch64 交叉编译，Windows 负责共享文件和
TFTP 服务，RK3568 只负责设备验证与运行 ARM64 产物。

**验证：** 编译产物经 `file` 检查为 ARM aarch64、statically linked，并在板端成功执行。

**面试表达：** 我先建立 host/build/target 边界，避免把“路径问题”和“编译问题”混为一谈。
这也是嵌入式交叉编译最基础的环境模型。

### P042：Windows 文件没有进入 Ubuntu，解压提示不存在

**现象：** `tar` 报 `/home/china/Downloads/...: No such file or directory`，且 `ls -lh ~/下载`
为空。

**根因：** 文件仍在 Windows 下载目录，VMware 拖放并没有完成复制。

**解决：** 先把文件放入 Ubuntu 主目录，或配置 VMware Shared Folders 并从
`/mnt/hgfs/WinDownloads` 访问。

**验证：** `ls -lh` 能看到 195 KiB 源码包，随后解压和交叉构建成功。

**面试表达：** 在执行解压前，我先用文件存在性和大小验证输入，避免对后续 tar 错误做无关排查。

### P043：VMware 拖放不可用，产物无法拖回 Windows

**现象：** Nautilus 拖动文件到 Windows 窗口无反应，或文件没有出现在 Windows 下载目录。

**根因：** VMware 拖放依赖 guest tools、桌面会话和宿主设置，可靠性不如显式共享目录。

**解决：** 使用 `/mnt/hgfs/WinDownloads` 与 `C:\Users\CC\Downloads` 的映射，用 `cp` 明确
复制，随后在两端用 `ls/Get-Item` 核对字节数。

**验证：** `rkav-gateway`、JSON 配置和后续版本产物均成功出现在 Windows 下载目录。

**面试表达：** 我把 GUI 拖放替换为可脚本化、可验证的共享目录传输，使部署步骤可重复。

### P044：BusyBox 缺少 `httpd`/`nc`，HTTP 传输方案失败

**现象：** `busybox httpd` 输出 `httpd: applet not found`，进程退出码 127；`which nc` 无输出。

**根因：** BusyBox 是按编译配置裁剪的多调用程序，命令名存在于其他发行版不代表当前固件包含
相应 applet。

**解决：** 用 `busybox --list` 先做能力盘点，发现 `tftp` 后改用仓库内 Python TFTP 单文件
服务工具。

**验证：** 58,120-byte JPG 和 1,920,044-byte WAV 均完整传回 Windows。

**面试表达：** 面对精简 Buildroot，我不假设常见 Linux 命令存在，而是先枚举固件真实能力，
再选择依赖最少的传输方案。

### P045：`eth0` carrier 为 0，板端与 Windows 不通

**现象：** `/sys/class/net/eth0/carrier` 为 0，内核输出 `Link is Down`，ping 超时。

**根因：** carrier 代表物理链路状态。网线松动、重插、对端网卡状态变化都会使其变为 0；即使
IP 配置仍保留也无法传输。

**解决：** 先处理网线和网口，再确认 carrier 为 1、板端 `192.168.50.2/24`、Windows
`192.168.50.1/24`，最后双向 ping。

**成功证据：** Windows ping 4/4，TTL=64；失败证据是 carrier=0 或板端 ping 100% loss。

**面试表达：** 网络排查按物理层、IP 层、端口/协议层进行，不在 carrier=0 时浪费时间检查应用。

### P046：TFTP 超时、服务端顺序和 Windows 防火墙

**现象：** Windows 显示 `Waiting for one TFTP ...`，板端 `tftp: timeout`；或者板端先发请求，
Windows 服务尚未启动。

**根因：** TFTP 使用 UDP 69 建立初始请求，服务端必须先监听；Windows Public 网络配置和防火墙
也可能丢弃入站 UDP。仓库脚本是单次服务，一次完成后会退出。

**解决：** 将直连网卡设为 Private，添加仅允许 `192.168.50.2` 访问 UDP 69 的规则；每传一个
文件先在 Windows 启动一次服务，再在板端执行一次 `tftp`。

**成功示例：** Windows 输出 `Sent N bytes` 或 `Received N bytes`，板端退出码 0 且目标文件存在。

**失败示例：** `TFTP upload timed out`、`tftp: timeout`，或服务端仍在等待且板端已经结束。

**面试表达：** 我明确了请求方向、单次服务生命周期和 UDP 防火墙边界，并用字节数而非“没有
报错”作为传输验收。

### P047：GitHub 连接失败导致 CMake FetchContent 中断

**现象：** `nlohmann/json.git` clone 三次失败，CMake 在 `FetchContent_MakeAvailable` 中止。

**根因：** Ubuntu 到 GitHub 443 的连接不稳定，而源码包没有完全 vendor 第三方依赖。

**解决：** 复用之前成功构建目录中的 `nlohmann_json-src` 和 `googletest-src`，通过
`FETCHCONTENT_SOURCE_DIR_*` 指向本地源码。

**验证：** CMake 显示使用本地 multi-header，配置耗时降至约 0.3 秒，后续无需访问 GitHub。

**面试表达：** 我把网络依赖从构建关键路径移除，建立可离线复现的交叉构建输入；长期方案应锁定
依赖版本并归档源码或使用依赖镜像。

### P048：`std::byte*` 不能 `static_cast` 为 `int16_t*`

**现象：** GCC 13 在 `alsa_audio_capture.cpp` 报 invalid `static_cast`。

**根因：** `std::byte*` 与 `int16_t*` 是无继承关系的对象指针，`static_cast` 不允许这种底层
表示重解释。

**解决：** 改用 `reinterpret_cast<std::int16_t*>(buffer->data())`，同时保证 buffer 对齐、容量
和音频样本布局契约正确。

**验证：** ARM64 静态构建通过，并在真实音频读取中完成 30 分钟验证。

**面试表达：** 这不是简单语法替换；底层 I/O buffer 需要显式表达表示转换，并由对齐、大小和
生命周期不变量保证安全。

### P049：静态链接 `getaddrinfo` 警告是否代表构建失败

**现象：** 链接 GoogleTest 时提示静态程序调用 `getaddrinfo` 运行时可能需要匹配 glibc 共享库。

**判断：** 这是 linker warning，不是构建失败。核心 `rkav-gateway` 和测试程序仍生成，脚本输出
`cross_build=passed`。

**处理：** 记录残余风险；用 `file` 验证产物，用板端实际运行和测试验证可执行性。该警告来自
GoogleTest streaming listener 路径，并不等价于主程序启动失败。

**面试表达：** 我区分 warning 和 error，并通过产物属性及目标机运行闭环，而不是看到“警告”
就宣称构建失败或完全忽略风险。

## 4. 真实视频与音频问题

### P050：视频节点选错、请求 30 FPS 但实测约 29 FPS

**现象：** 摄像头出现 `/dev/video9` 和 `/dev/video10`；持续采集启动时报告一次 dropped buffer，
最终约 29.11 FPS。

**根因：** `/dev/video10` 是 UVC metadata capture，不是画面节点。配置中的 30 FPS 是请求值；
启动、USB 调度和工具统计窗口会影响短测平均值。

**解决：** 通过 `v4l2-ctl --all` 和 `--list-formats-ext` 识别节点能力；代码读取驱动协商值；用
300 帧和 60 秒运行量化稳态结果。

**验证：** 项目 60 秒取得 1,741 帧，约 29.0 FPS；30 分钟取得 52,340 帧，约 29.08 FPS。

**面试表达：** 我不把请求能力写成实测性能，用协商结果和长时间计数给出约 29 FPS 的准确结论。

### P051：`arecord --periods` 不受旧版工具支持

**现象：** BusyBox/旧版 `arecord` 报 `unrecognized option '--periods=4'`。

**根因：** 不同 alsa-utils 版本的命令行选项不同，桌面 Linux 教程不能直接套到旧 Buildroot。

**解决：** 最小能力测试只使用已支持的 `-D -f -r -c -d`；period/buffer 的精确设置在项目
ALSA 后端通过内核 UAPI 完成。

**验证：** 简化命令录音 2 秒退出码 0，生成约 376 KiB WAV。

**面试表达：** 我把工具版本差异与设备能力分开，先用最小参数证明设备可用，再在代码中控制
真正需要的硬件参数。

### P052：USB Audio `cannot get freq` 警告

**现象：** 每次打开 UAC 端点时内核输出 `cannot get freq at ep 0x84`。

**判断：** 驱动无法通过该控制请求读取端点频率，但本设备仍能按 48 kHz 双声道完成精确时长
录音和 30 分钟采集。它是设备/旧 UAC 驱动兼容警告，不是本次采集失败的充分证据。

**处理：** 不屏蔽日志；用帧数、WAV 长度、PTS、XRUN/错误指标和长稳继续监控。

**面试表达：** 我没有凭一条内核警告判断成功或失败，而是用数据完整性和长期行为评估实际影响，
并保留该兼容性风险。

### P053：ALSA `HW_REFINE` 返回 `EINVAL`

**现象：** 视频后端成功打开后，音频在 `SNDRV_PCM_IOCTL_HW_REFINE` 处返回 native code 22。

**根因：** 直接使用 ALSA 内核 UAPI 时，`snd_pcm_hw_params` 的 mask 和 interval 不能只清零后
随意填写；必须按内核约定初始化为允许范围，再收紧 access、format、channels、rate、period
和 buffer 条件。

**解决：** 完整初始化 hardware parameters，并按 refine -> constrain -> hw_params 的状态机
协商，而不是照搬 libasound 高层 API 的使用假设。

**验证：** 后端输出协商后的 `S16_LE/48000/2/960 samples`，10 秒采集 500 块。

**面试表达：** 为避免旧 Buildroot ABI 问题我选择内核 UAPI，代价是必须自己遵守 ALSA 参数空间
和 PCM 状态机；我用 arecord 能力证据先排除了硬件不支持。

### P054：ALSA Capture 已打开但读取链路不正确

**问题一：** 设置参数后没有显式 `START`，设备不一定进入 RUNNING。

**问题二：** `SNDRV_PCM_IOCTL_READI_FRAMES` 的 ioctl 返回值表示系统调用成功/失败，实际帧数在
`snd_xferi.result`；把返回值当帧数会产生零进展或错误计数。

**解决：** 参数和 prepare 完成后显式启动 Capture；读取后检查 `transfer.result`，处理短读和
负错误码。

**验证：** 20 ms 帧持续达到每秒 50 块，60 秒精确得到 3,000 块。

**面试表达：** 我通过“设备打开成功但业务无进展”的指标继续向 PCM 状态机和 ioctl 数据契约
定位，而没有把 open 成功等同于采集完成。

### P055：音频 PTS 不能使用读取完成时刻直接生成

**影响：** USB 调度和线程唤醒抖动会直接进入 PTS，导致音频时间轴不连续，后续 A/V 同步困难。

**解决：** 以首次采集时间为基准，用累计采样帧数和采样率计算后续 PTS。48 kHz、20 ms 每块
应推进 960 个 sample frame。

**验证：** 每秒稳定得到 50 个音频块，长稳 90,000 块，计数和 30 分钟严格一致。

**面试表达：** 音频时间本质由样本时钟定义；完成时刻适合衡量 I/O 延迟，不适合代替媒体时间轴。

### P056：SIGINT/SIGTERM、定时退出和错误退出混淆

**正确语义：**

| 场景 | 退出码 | reason | 队列 |
|---|---:|---|---|
| `--duration` 到期 | 0 | `run_duration_elapsed` | 关闭并排空 |
| SIGINT/SIGTERM | 0 | `signal` | 关闭并排空 |
| USB 断连导致健康失败 | 1 | `fatal_error` | 关闭并排空已入队数据 |

**验证：** 真实设备下 SIGTERM 和 SIGINT 均完成；USB 断连无需外部强停，`forced_stop=0`。

**面试表达：** 优雅退出不等于所有退出码都为 0；硬件致命故障应该非零退出，但仍要保证资源
释放、线程收敛和已接收数据一致处理。

### P057：ALSA `POLLERR` 把断连误判成 XRUN

**现象：** 拔下 USB 后，第一版把音频 `POLLERR` 统一走 XRUN 恢复，错误语义不准确。

**根因：** `poll` revents 只说明 fd 有异常，不能独立区分缓冲欠载、挂起和设备消失。

**解决：** 收到 `POLLERR` 后执行 `SNDRV_PCM_IOCTL_STATUS`，根据 PCM state 和 errno 分类；
`ENODEV` 映射为 `device_disconnected`，并记录恢复失败上下文。

**验证：** v7 拔线时 ALSA 报 native code 19，V4L2 同时报断连，应用在健康超时后自行退出。

**面试表达：** 我将 OS 事件转换为业务错误前补做状态查询，避免错误恢复策略掩盖真正根因。

## 5. 工具、证据和终端问题

### P058：BusyBox `tar` 不支持 `-z`

**现象：** `tar -czf` 报 `invalid option -- 'z'`。

**根因：** 当前 BusyBox tar 没编译显式 gzip 选项，但支持 `-a` 根据扩展名选择压缩方式。

**解决：** 使用 `tar -caf file.tar.gz ...`，再用 `tar -tf` 校验目录清单。

**验证：** 2.6 MiB 真实 A/V 证据包创建成功并传回 Windows，SHA-256 已记录。

**面试表达：** 我先读工具自己的 usage，再选固件支持的参数；归档后同时验证退出码、文件存在
和内容清单。

### P059：首条长稳资源样本异常低

**现象：** `elapsed=0` 时 RSS 为 4 KiB、线程数为 1，10 秒后变为 9,728 KiB 和 9 线程。

**根因：** 监控脚本在子进程完成初始化前抢先读取 `/proc`，属于启动采样竞态，并非运行中突然
增长或内存泄漏。

**解决：** 将稳态分析窗口从初始化完成后的样本开始；同时保留原始首条数据，不篡改证据。

**验证：** 第 10 秒到 1,793 秒 RSS 始终 9,728 KiB、线程数始终 9。

**面试表达：** 指标必须结合生命周期解释。我区分启动瞬态和稳态窗口，避免用单点数据得出泄漏
结论。

### P060：SecureCRT 无法输入：流控、会话或内核故障

**现象：** 窗口有黑色光标但无法输入，Ctrl+C 也无效。

**可能层次：**

1. 误触 Ctrl+S，终端软件 XON/XOFF 暂停显示，可尝试 Ctrl+Q。
2. SecureCRT 会话失去串口，需断开并重连 COM5。
3. 板端前台程序或内核卡死，串口输入不会被调度。
4. 出现 kernel panic/oops 后系统已不再可靠，Ctrl+C 无法中断内核故障。

**处理顺序：** Enter -> Ctrl+Q -> SecureCRT 断开/重连 -> 观察启动日志 -> 只有系统已无响应时
才断电重启。若刚出现内核 oops，停止继续写盘或重复网络压力操作。

**面试表达：** 我把终端显示问题、串口连接问题、用户态阻塞和内核故障分层，而不是一律重启。

### P061：大文件 TFTP 触发网卡 watchdog 和内核异常

**影响：** 7.7 MiB 板端上传超时，网卡复位；重试后板端可能失去响应，存在系统和文件系统风险。

**关键证据：**

```text
NETDEV WATCHDOG: eth0 (rk_gmac-dwmac): transmit queue 0 timed out
rk_gmac-dwmac ... Reset adapter
__memcpy -> dev_gro_receive -> napi_gro_receive -> stmmac_napi_poll
```

**根因判断：** 当前证据强烈指向旧 4.19.232 BSP 中 `rk_gmac-dwmac/stmmac` 数据路径在持续传输
下的驱动缺陷或内存破坏。不能仅凭现有日志宣称已经定位到具体代码行，但已排除普通文件不存在
这一层问题。

**处置：** 停止使用 TFTP 回传大文件；不在发生 oops 后继续重试；使用存储卡/读卡器或从匹配
SDK 重新取得文件。板端现有模型直接原地使用。

**验证状态：** 规避策略已确定，驱动根因未修复。后续若要修复必须取得匹配 BSP 源码、驱动配置
和可复现压力测试环境。

**面试表达：** 我不会把规避写成修复。发现内核网络栈异常后，我先保护设备和证据，缩小工作
范围，并把业务主线改为不依赖板端大文件上传。

### P062：多行命令、`>>` 提示符和 `$?` 取值误区

**现象：** PowerShell 或 shell 出现 `>>`/`>` 后看似“卡住”；命令执行完很久后再 `echo $?`，
得到的是中间命令而非目标命令的退出码。

**根因：** `>>`/`>` 表示语句尚未闭合，常见原因是引号、花括号、管道或续行符未结束。`$?`
只保存最近一条命令的退出状态。

**解决：** 多行命令完整粘贴并确保闭合；输错时 Ctrl+C 回到主提示符。关键命令后立刻保存：

```sh
some_command
rkav_exit=$?
echo "exit=$rkav_exit"
```

PowerShell 对外部程序则应立即读取 `$LASTEXITCODE`。

**面试表达：** 自动化脚本必须在目标命令后立即捕获退出码，否则后续 `echo`、`ls` 会覆盖真正
状态并产生假阳性。

## 6. RKNN 接入问题

### P063：RKNN 只有 Runtime 和模型，没有头文件

**现象：** 板端有 `/usr/lib/librknnrt.so` 和 `.rknn` 模型，但找不到 `rknn_api.h`。

**解释：** 运行镜像通常只部署运行库和模型，不包含开发头文件。头文件属于 host 侧 SDK，用于
Ubuntu 交叉编译，不要求出现在板端。

**解决：** 从与板端 Runtime 1.4.0 匹配的厂商 SDK 或官方 Toolkit2 v1.4.0 获取开发文件；板端
继续使用已有 Runtime 和模型路径。

**面试表达：** 我区分开发时依赖和部署时依赖：头文件服务于编译，动态库和模型服务于目标机
运行。

### P064：最新 RKNN SDK/模型不能直接配旧 Runtime

**风险：** 最新头文件、Runtime、模型格式和 2022 年板端驱动之间可能存在 ABI、模型版本或最低
驱动版本不兼容。

**证据：** 板端 Runtime 字符串明确为 1.4.0，NPU 驱动为 0.8.2；库内部还包含模型版本和最低
驱动版本不匹配提示。

**解决：** 建立版本矩阵：驱动、Runtime、头文件、Toolkit、模型逐项锁定。优先完全匹配 1.4.0，
不要单独覆盖板端 `/usr/lib/librknnrt.so`。

**面试表达：** NPU 栈是垂直版本契约，不是普通业务库升级。我先固定整条版本链，再做最小样例，
避免把兼容问题带入主程序。

### P065：板端 MobileNet 与项目目标检测接口不匹配

**现象：** 板端只有 `mobilenet_v1.rknn`，而项目推理接口输出包含 bounding box 的
`DetectionBatch`。

**根因：** MobileNet v1 通常是图像分类模型，输出类别概率；目标检测模型还需要框回归、置信度
和 NMS 后处理。两者输出语义不同。

**解决：** MobileNet 只用于验证 RKNN Runtime/NPU 能运行；主程序必须选择兼容 RK3568 和
Runtime 1.4.0 的检测模型，并明确预处理、量化和后处理契约。

**面试表达：** “NPU 跑通”与“业务模型接入”是两个里程碑。我先用板端现有模型证明基础设施，
再用目标检测模型满足接口语义。

### P066：MJPEG payload 不能直接送入 RKNN

**现象：** V4L2 当前输出 MJPEG 1280x720，而多数检测模型输入为固定尺寸 RGB/BGR/NHWC/NCHW
tensor。

**根因：** MJPEG buffer 是压缩码流，不是逐像素数组；还缺少 JPEG 解码、颜色转换、resize/
letterbox、量化以及坐标逆映射。

**解决顺序：** 固定图片 CPU 参考预处理 -> 校验 tensor 和检测框 -> 接 JPEG 解码 -> 接实时
视频 -> 指标证明 CPU 成为瓶颈后再用 RGA/DMA-BUF 优化。

**面试表达：** 我不把摄像头 buffer 和模型 tensor 混为一谈。预处理既是数据格式转换，也是
几何契约，错误会造成模型“能跑但结果错”。

## 7. 本阶段问题回答模板

面试时每个问题按以下顺序回答：

```text
背景约束
-> 业务影响
-> 最小复现和关键证据
-> 排除了什么
-> 根因或当前最强假设
-> 方案与取舍
-> 测试和指标
-> 尚未解决的风险
```

不要说“多试了几个版本就好了”。应明确指出，例如 ALSA 问题是如何从 arecord 成功、项目
`HW_REFINE EINVAL`，进一步定位到内核 UAPI 参数初始化，再通过 10 秒、60 秒和 30 分钟验证
闭环的。

## 8. 相关文档

- [真实音视频联调对话总结与下一步方案](12-真实音视频联调总结与下一步.md)
- [真实音视频与 RKNN 阶段问题汇总：通俗版](14-真实音视频与RKNN阶段问题汇总-通俗版.md)
- [UGREEN 2K USB 音视频设备验收](11-绿联摄像头与麦克风验收.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md)
