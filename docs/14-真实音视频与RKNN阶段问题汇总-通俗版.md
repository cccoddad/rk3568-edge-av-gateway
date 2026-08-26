# 真实音视频与 RKNN 阶段问题汇总：通俗版

更新日期：2026-08-26

## 1. 先看懂这三台“电脑”

这个项目最容易犯的错误，是没有先看命令要在哪个窗口执行。

| 你看到的开头 | 这是什么 | 应该做什么 |
|---|---|---|
| `PS C:\...>` | Windows PowerShell | 找下载文件、配置网卡、运行 TFTP 工具 |
| `china@ubuntu:~$` | Ubuntu 虚拟机 | 解压源码、编译 ARM64 程序 |
| `root@RK356X:/#` | RK3568 开发板 | 查看摄像头、录音、运行编译好的程序 |

可以把它们理解成：

```text
Windows = 文件中转站
Ubuntu = 生产 ARM64 程序的工厂
RK3568 = 真正使用摄像头和运行程序的设备
```

一条命令看起来完全正确，如果放错窗口执行，仍然会失败。

## 2. 文件为什么总是“找不到”

### 问题 1：Windows 下载目录里的文件，Ubuntu 为什么看不到

因为 Windows 和 Ubuntu 是两个文件系统。Windows 的：

```text
C:\Users\CC\Downloads
```

不等于 Ubuntu 的：

```text
/home/china/Downloads
```

本项目后来使用 VMware 共享文件夹。两边实际对应关系是：

```text
Windows: C:\Users\CC\Downloads
Ubuntu:  /mnt/hgfs/WinDownloads
```

在 Ubuntu 执行下面命令能看到文件，才说明共享成功：

```sh
ls -lh /mnt/hgfs/WinDownloads
```

**成功：** 列表中有目标文件，大小不是 0。

**失败：** `No such file or directory`，说明共享目录没挂载或路径写错；列表为空则说明文件还没
放进 Windows 下载目录。

### 问题 2：为什么拖放文件有时完全没反应

VMware 的鼠标拖放依赖虚拟机工具和桌面会话，容易失效。后来改用共享目录和 `cp`，因为命令
执行后可以明确检查文件大小。

Ubuntu 复制到 Windows 的例子：

```sh
cp out/aarch64-static/rkav-gateway /mnt/hgfs/WinDownloads/rkav-gateway
ls -lh /mnt/hgfs/WinDownloads/rkav-gateway
```

看到文件和大小才算成功，不要只凭“命令没有红字”。

### 问题 3：`tar` 说没有那个文件怎么办

不要立刻换一堆解压参数，先检查输入文件：

```sh
ls -lh /完整/路径/文件.tar.gz
```

- 能列出文件：再执行 `tar`。
- `No such file or directory`：先处理路径或文件传输，tar 本身没有问题。

## 3. 板端为什么很多常见命令不能用

### 问题 4：`busybox httpd` 为什么提示 applet not found

BusyBox 像一个可以装很多小工具的工具箱，但厂家可以只放其中一部分。别人的 BusyBox 有
`httpd`，不代表这块板子的 BusyBox 也有。

查看当前板端到底有哪些工具：

```sh
busybox --list
```

本板没有 `httpd` 和可用的 `nc`，但有 `tftp`，所以后来改用 TFTP。

### 问题 5：`tar -czf` 为什么报 `invalid option -- 'z'`

当前 BusyBox tar 不支持单独的 `-z`，但支持 `-a` 根据 `.tar.gz` 后缀自动压缩：

```sh
tar -caf evidence.tar.gz file1.log directory
```

然后必须检查：

```sh
rkav_tar_exit=$?
echo "tar_exit=$rkav_tar_exit"
ls -lh evidence.tar.gz
tar -tf evidence.tar.gz | head
```

**成功：** `tar_exit=0`，文件存在，`tar -tf` 能列出里面的文件。

**失败：** 退出码非 0、文件不存在，或清单里缺少关键证据。

### 问题 6：`arecord --periods=4` 为什么不认识

板端的 `arecord` 比较旧，不支持一些新版本参数。最小录音测试使用它认识的参数即可：

```sh
arecord -D hw:2,0 -f S16_LE -r 48000 -c 2 -d 2 /tmp/test.wav
echo "arecord_exit=$?"
ls -lh /tmp/test.wav
```

**成功：** `arecord_exit=0`，2 秒双声道 48 kHz WAV 大约 376 KiB。

**失败：** 出现 `unrecognized option` 是工具参数问题；`No such file or directory` 或设备错误才要
继续检查声卡编号和 USB 连接。

## 4. 网络和 TFTP 到底怎么判断

### 问题 7：`carrier` 输出 0 和 1 分别是什么意思

板端执行：

```sh
cat /sys/class/net/eth0/carrier
```

- `1`：网线物理链路已建立。
- `0`：网线、网口或对端网卡没有连通。

carrier 为 0 时，即使 IP 地址还显示 `192.168.50.2`，也传不了文件。先重新插好网线，再检查：

```sh
ip -4 addr show eth0
ping -c 4 192.168.50.1
```

### 问题 8：TFTP 命令到底先在哪边运行

以 Windows 给板端发送文件为例：

1. **先在 Windows PowerShell** 启动发送工具，看到：

```text
Waiting for one TFTP download request on 192.168.50.1:69 ...
```

2. **再到 RK3568 板端** 执行 `tftp -g` 下载。

3. Windows 出现 `Sent N bytes`，板端文件存在，才算完成。

反过来，板端向 Windows 上传时，Windows 先显示：

```text
Waiting for one TFTP upload on 192.168.50.1:69 ...
```

然后板端执行 `tftp -p`。

### 问题 9：为什么 Windows 一直 Waiting

`Waiting` 不是错误，它表示服务已经准备好，正在等板端请求。常见失败原因：

- 板端还没执行对应的 `tftp`。
- 网线掉线，carrier 变成 0。
- Windows 防火墙挡住 UDP 69。
- 方向写反：Windows 等 upload，板端却也在等 download。
- 文件传完一次后脚本已经退出，又传第二个文件却没有重新启动脚本。

**成功输出：** `Sent ... bytes` 或 `Received ... bytes`。

**失败输出：** `TFTP upload timed out`、`tftp: timeout`。

### 问题 10：为什么后来不让再用 TFTP 传大文件

因为最后已经不是普通超时。板端出现了：

```text
NETDEV WATCHDOG: eth0 ... transmit queue 0 timed out
... Reset adapter
```

重试后还出现网卡驱动 `stmmac` 接收路径的内核异常。通俗地说，不是快递地址写错，而是板端
负责网口的“运输系统”在连续搬大文件时自己出故障了。

以后规则：

- 小文件可以谨慎使用 TFTP，传完校验大小或哈希。
- 数 MiB 的板端回传不再用这个网口反复尝试。
- RKNN 板端模型直接使用原路径，不需要先传出来。
- 大文件使用存储卡和读卡器，或从匹配版本的官方 SDK 重新下载。
- 看到 `WATCHDOG`、`Reset adapter`、`Call trace`、`Oops` 后立即停止网络压力操作。

这叫“规避风险”，还不能叫“修好了网卡驱动”。

## 5. 摄像头为什么有两个 video 节点

### 问题 11：`/dev/video9` 和 `/dev/video10` 该用哪个

本次实测：

- `/dev/video9` 是真正的画面采集节点。
- `/dev/video10` 是摄像头附带的元数据节点，不会给出正常图像。

不能只看编号猜，应该执行：

```sh
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video9 --all
v4l2-ctl -d /dev/video10 --all
```

看到 `Video Capture` 才是画面；看到 `Metadata Capture` 不是画面节点。

### 问题 12：明明设置 30 FPS，为什么只有 29 FPS 左右

30 FPS 是要求摄像头尽量按这个速率工作，不代表任何 10 秒窗口都精确得到 300 帧。启动缓冲、
USB 调度和统计窗口都会造成差异。

本次实测不是失败：

- 300 帧工具测试完成，启动阶段丢 1 个 buffer，最终约 29.11 FPS。
- 项目 60 秒得到 1,741 帧，约 29.0 FPS。
- 项目 30 分钟得到 52,340 帧，约 29.08 FPS。

正确说法是“720p MJPEG 稳态接近 30 FPS”，不能写“严格 30 FPS、零丢帧”。

### 问题 13：为什么先选 MJPEG 720p，不直接用 2K

MJPEG 是摄像头已经压缩过的数据，USB 带宽压力较低。720p 也更容易先证明程序正确。2K 会
增加解码、预处理和推理压力，应该在基础链路稳定后再做。

## 6. 麦克风和 ALSA 为什么修了很多版

### 问题 14：`cannot get freq at ep 0x84` 是录音失败吗

不一定。本次内核每次打开麦克风都打印这条警告，但：

- 2 秒 WAV 成功。
- 10 秒 WAV 成功。
- 项目 60 秒得到 3,000 个 20 ms 音频块。
- 项目 30 分钟得到 90,000 个音频块。

所以当前结论是“存在 USB 音频兼容警告，但没有阻止采集”。以后仍需观察，不能直接删除或忽略
这条日志。

### 问题 15：系统 `arecord` 能录，项目为什么还会 `EINVAL`

这正好说明麦克风和驱动基本没问题，错误更可能在项目自己的 ALSA 参数设置。

项目为了做静态 ARM64 程序，直接和 Linux ALSA 内核接口通信。这个接口比 `arecord` 使用的
高级库更底层，参数结构必须按内核规则初始化。第一版初始化不完整，所以 `HW_REFINE` 返回 22，
也就是 `Invalid argument`。

修复后程序能够打印：

```text
backend=alsa
format=S16_LE
sample_rate=48000
channels=2
samples_per_frame=960
```

并且 10 秒得到 500 块，这才说明项目音频后端真正工作。

### 问题 16：为什么打开设备成功还不等于能录音

打开只是拿到设备句柄，后面还要：

```text
协商参数 -> 准备 -> 启动 -> 等待数据 -> 读取实际帧数
```

本轮还修复了两个关键细节：

- Capture 参数设置完后要显式启动。
- ioctl 是否成功和“这次读到多少帧”是两个值，实际帧数在 `transfer.result` 中。

就像打开水龙头总阀不等于水已经流到杯子里，必须检查整个流程是否有持续数据。

### 问题 17：为什么 20 ms 音频应该每秒 50 块

因为：

```text
1000 ms / 20 ms = 50
```

48 kHz 下每个 20 ms 块应该有：

```text
48000 * 0.020 = 960 个 sample frame
```

因此：

- 10 秒应约 500 块。
- 60 秒应约 3,000 块。
- 30 分钟应约 90,000 块。

本项目三个数字全部对上，这是比“日志没报错”更有力的成功证据。

### 问题 18：音频时间戳为什么要按样本数算

线程每次醒来的时间会有抖动。如果用“读取完成的当前时间”当音频 PTS，时间轴会跟着操作系统
抖动。音频本身有固定采样率，所以应该用已经采集的样本数来推进时间。

简单理解：音频时间看“录了多少个样本”，不是看“线程什么时候拿到这批样本”。

## 7. 停止和拔线为什么也要专门测试

### 问题 19：Ctrl+C、SIGTERM 和运行时长到期有什么区别

| 情况 | 正常结果 |
|---|---|
| `--duration 10` 到期 | 退出码 0，原因 `run_duration_elapsed` |
| Ctrl+C / SIGINT | 退出码 0，原因 `signal` |
| SIGTERM | 退出码 0，原因 `signal` |
| USB 拔线 | 退出码 1，原因 `fatal_error` |

USB 故障退出码为 1 是正确的，因为它确实发生了错误。“优雅退出”不是所有情况都返回 0，而是
程序能结束线程、释放设备、关闭队列并保留真正原因。

### 问题 20：拔 USB 后为什么第一版说是 XRUN

`poll()` 只告诉程序“这个音频设备有异常”，它没有直接说异常是：

- 数据来不及读，发生 XRUN；
- 设备暂停；
- USB 已经被拔掉。

第一版把它们都当 XRUN。修复后程序会再查询一次 PCM 状态。v7 实测拔线时正确输出：

```text
category=device_disconnected
native_code=19
```

19 是 `ENODEV`，意思是设备不存在。随后程序在健康检查超时后自行退出，`forced_stop=0`。

### 问题 21：重新插入为什么还要再跑 10 秒

插回来并看到设备节点，只能说明系统重新识别了 USB。还要确认程序可以再次打开它、持续采集
并正常退出。

v7 重插后的 10 秒结果：

- 音频 500 块。
- 视频 285 帧。
- 路由和消费 785 包。
- 错误、恢复、队列丢弃均为 0。

这才算重插后的完整复验。

## 8. 长稳数据怎么看

### 问题 22：第一行 RSS 只有 4 KiB，是程序突然涨内存了吗

不是。监控脚本刚启动程序就立即读了 `/proc`，当时程序还没完成初始化，所以只看到 1 个线程
和 4 KiB。10 秒后程序进入稳定状态：9 个线程、9,728 KiB。

从第 10 秒到第 1,793 秒，RSS 和线程数一直没变。因此应该把第一条看成“启动瞬间样本”，不能
拿它和稳定运行数据直接比较。

### 问题 23：30 分钟怎样才算成功

本次成功证据不是“窗口一直在刷日志”，而是：

- 脚本退出码 0。
- 90,000 音频块全部编码。
- 52,340 视频帧全部编码。
- 142,340 个包全部路由并消费。
- 错误、恢复、队列丢弃全为 0。
- RSS、线程数稳定。
- 温度约 48.9～50.6 C，没有触发热保护。

这些数字互相能对上，说明管道中没有悄悄丢掉已经接受的数据。

## 9. PowerShell 和终端为什么像“卡住”

### 问题 24：看到 `>>` 或 `>` 是什么意思

它通常不是程序卡死，而是命令还没写完。例如少了右引号、右花括号，或上一行用续行符告诉
终端“下一行继续”。

- PowerShell 常见续行符是反引号 `` ` ``。
- Linux shell 常见续行符是反斜杠 `\`。
- 输入错误可用 Ctrl+C 放弃这条未完成命令，回到正常提示符。

不要把 Windows 的反引号和 Linux 的反斜杠混用。

### 问题 25：为什么 `echo $?` 有时不是刚才那条命令的结果

`$?` 只记住最近一条命令。目标命令后如果又执行了 `ls`、`echo`，状态就可能被覆盖。

Linux 正确写法：

```sh
目标命令
rkav_exit=$?
echo "exit=$rkav_exit"
```

Windows PowerShell 执行 Python 等外部程序后，立即看：

```powershell
$LASTEXITCODE
```

### 问题 26：SecureCRT 点不动、Ctrl+C 也没用怎么办

按这个顺序处理：

1. 点一下终端正文，按 Enter。
2. 按 Ctrl+Q，解除可能误触 Ctrl+S 造成的暂停。
3. 在 SecureCRT 中断开当前会话，再连接 COM5。
4. 如果刚才屏幕出现 `Call trace`、`Oops` 或内核 panic，说明不是普通终端问题。
5. 内核已经完全无响应时只能断电重启；重启后不要立刻重复触发故障的操作。

Ctrl+C 只能中断用户态前台程序，不能保证从内核崩溃中恢复。

## 10. RKNN 现在到底进行到哪了

### 问题 27：板端找不到 `/dev/rknpu`，是不是没有 NPU

不是。本板使用 DRM 方式暴露 NPU，已经看到：

```text
/dev/dri/card1
/dev/dri/renderD129
rknpu 0.8.2 20220829
```

因此不能只凭 `/dev/rknpu` 不存在就下结论。

### 问题 28：为什么板端有 `librknnrt.so`，却没有 `rknn_api.h`

`.so` 是程序运行时使用的库，`.h` 是写代码和编译时使用的说明文件。生产板镜像通常只放运行
需要的 `.so`，不放开发头文件。

下一步要在 Ubuntu 中准备与板端 1.4.0 Runtime 匹配的头文件和链接库，而不是要求板端一定有
头文件。

### 问题 29：能不能直接下载 GitHub 最新版本

不能直接混用。当前板端是：

```text
NPU driver: 0.8.2 (2022-08-29)
RKNN Runtime: 1.4.0 (2022-09-09)
```

最新 SDK 生成的模型可能要求更新 Runtime 或驱动。像手机系统、应用和文件格式一样，版本跨度
太大可能互不认识。

正确做法是优先找同版本 BSP/SDK 或官方 Toolkit2 v1.4.0 的开发文件，先跑最小样例。不能只把
板端一个 `.so` 替换成最新版来碰运气。

### 问题 30：板端已经有 MobileNet，为什么还不能说目标检测完成

板端的 `mobilenet_v1.rknn` 通常做图片分类，比如判断“这张图最像猫、汽车还是杯子”。项目需要
的是目标检测，要输出物体位置框、类别和置信度。

所以它们用途不同：

- MobileNet：先证明 RKNN Runtime 和 NPU 可以跑。
- 检测模型：以后真正接到项目 `DetectionBatch`。

### 问题 31：摄像头 MJPEG 为什么不能直接给 NPU

MJPEG 是压缩后的 JPEG 数据，模型通常要的是固定宽高的 RGB/BGR 数字矩阵。中间至少需要：

```text
JPEG 解码
-> 颜色转换
-> resize 或 letterbox
-> 量化/归一化
-> RKNN tensor
```

这一步如果错了，程序可能“运行成功”但识别结果完全错误。所以先用固定图片做 CPU 参考结果，
确认正确后再接实时摄像头和 RGA 加速。

## 11. 下一步只做什么

当前不要继续压缩和用 TFTP 回传 RKNN 大文件。下一步按下面顺序：

1. 保留 v7 的真实 A/V 10 秒和 30 分钟基线。
2. 在 Ubuntu 准备匹配 Runtime 1.4.0 的 RKNN 开发文件。
3. 写一个独立小程序，在板端直接读取
   `/usr/share/model/RK356X/mobilenet_v1.rknn`。
4. 先证明模型能初始化、查询输入输出并运行 100 次。
5. 再找匹配版本的目标检测模型，用固定图片验证。
6. 实现项目的 `RknnInferenceEngine`，保留 Mock 开关。
7. 最后接 MJPEG 解码和实时摄像头，重复 10 秒、60 秒、30 分钟和拔线测试。

详细计划见
[真实音视频联调对话总结与下一步方案](12-真实音视频联调对话总结与下一步方案.md)。

## 12. 新手最重要的十条规则

1. 执行命令前先看提示符，确认是 Windows、Ubuntu 还是 RK3568。
2. 解压或发送前先 `ls/Get-Item` 确认文件真实存在且大小正确。
3. 板端是 BusyBox，先看工具 usage，不照搬桌面 Ubuntu 参数。
4. 网络先看 carrier，再看 IP，再 ping，最后才检查 TFTP。
5. Windows TFTP 工具要先启动；它一次通常只传一个文件。
6. `arecord` 成功可以排除很多硬件问题，但不代表项目 ALSA 代码一定正确。
7. 设备 open 成功不等于持续采集成功，要看帧数、块数、错误和退出状态。
8. 一条 warning 不等于失败，一条成功日志也不等于全链路成功，要看完整证据。
9. 看到 kernel watchdog/oops 后停止重试，先保护板子和数据。
10. RKNN 的驱动、Runtime、头文件、转换工具和模型必须按版本成套考虑。

## 13. 相关文档

- [真实音视频联调对话总结与下一步方案](12-真实音视频联调对话总结与下一步方案.md)
- [真实音视频与 RKNN 阶段问题汇总：面试版](13-真实音视频与RKNN阶段问题汇总-面试版.md)
- [UGREEN 2K USB 音视频设备验收](11-ugreen-camera-and-microphone-validation.md)
- [项目问题汇总：通俗版](07-项目问题汇总-通俗版.md)
