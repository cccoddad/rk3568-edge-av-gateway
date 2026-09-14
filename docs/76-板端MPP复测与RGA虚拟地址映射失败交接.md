# 板端 MPP/RGA 复测：MPP 配置修复通过、RGA 虚拟地址映射失败交接

更新日期：2026-09-14
状态：**MPP 配置键修复已在板端确认有效；新失败点转到 RGA RGB→NV12 虚拟地址路径（P122），
修复方案已定向，等待重建候选后复测**。本文件是最新会话交接，替代
`docs/75` 中的板端待办描述继续向下推进。

## 1. 一句话结论

2026-09-14 在真实板端执行了 `docs/67` 第 5 节的 10 秒三硬件 + MPP/RGA 复测。修复候选
（ELF SHA-256 `afaafe6733d287d7519c93877494108a514eabd7128f7778473ce7c580bf9acd`）不再被 MPP
配置阶段拒绝：V4L2、ALSA、MPP 编码器初始化、15 项配置键、`MPP_ENC_SET_CFG` 与
`MPP_ENC_SET_HEADER_MODE` 全部通过，管道进入运行态；但第一帧 RGB→NV12 转换被 RGA 运行时
拒绝（`RGA_BLIT fail: Bad address`），内核 RGA2 驱动同时报
`rga2_mmu: RGA2 failed to get pte` 与 `rga2 map src0 memory failed`。没有任何 H.264 输出
（输出 `.part` 为 0 字节），网关安全退出（退出码 1、无残留进程、零恢复）。全部失败证据已
锁定并归档。

## 2. 板端接入方式（本轮新打通，供后续会话复用）

- SSH(22) 仍是 P032 的 banner 超时；COM5 串口线当前未插（Windows 显示 Code 45 幽灵设备）。
- 本轮改用板端 **ADB over TCP 5555**：Windows 侧 Android SDK platform-tools 执行
  `adb connect 192.168.50.2:5555` 成功；`adb devices` 同时列出 USB gadget transport
  `603ac2909d6146a7` 与 TCP transport，两者 `uname`/`hostname` 完全一致，确认为同一块板。
- 该通道用于只读检查、`adb push` 部署、`adb pull` 取证与受控测试执行；未停止/重启任何
  板端服务，未刷写，未改板端系统文件（仅临时校时，见下）。
- 注意：Windows 侧 adb 会剥离命令中的双引号，含 `|`、`\r` 等的 shell 片段要写成脚本文件
  后 `adb push` 执行，不要内联拼命令。

## 3. 运行前门禁（只读，全部通过）

| 检查项 | 结果 |
|---|---|
| 板端身份 | `Linux RK356X 4.19.232 aarch64`，root shell，5.10 未刷入 |
| 遗留进程 | `pidof rkav-gateway` 为空；无 rkav 相关进程 |
| 设备节点 | `/dev/mpp_service`、`/dev/rga`、`/dev/dri/card1`、`renderD129` 存在 |
| MPP 运行库 | `/usr/lib/librockchip_mpp.so.0` SHA-256 `49c9cf2b…ddcbca`，与 docs/27 一致 |
| RGA 运行库 | `/usr/lib/librga.so.2.1.0` SHA-256 `21b65ea2…f0bd8`，与 docs/27 一致 |
| 摄像头/麦克风 | 复插后 `1-1.1 0c45:636f UGREEN Camera 2K`；`/dev/video9`；ALSA card 2 `pcmC2D0c` |
| 模型 | `/userdata/rkav/yolov5-runtime-712d661/yolov5s-rk3568.rknn`（8,688,000 字节） |
| 磁盘 | `/userdata` 空闲 7.8 GB；旧失败目录 `mpp-rga-abi-19700101-021545-755` 完整保留 |
| RTC/时间 | 板端时间为 1970（老问题）；本轮以 `date -s @<epoch>` 临时校时到 2026-09-14 02:56:33 UTC |

## 4. 复测部署与执行

- 唯一结果目录：`/userdata/rkav/mpp-rga-retest-20260914-105633-6922`。
- 部署内容：修复候选 `rkav-gateway`（哈希复核一致）、`FIX_INPUTS.provenance`、
  `SHA256SUMS`、配置 `rk3568-rknn-mjpeg-alsa-mpp-h264.json`（仅把输出路径改为该目录内
  `mpp-h264-evidence.h264`，修改后 SHA-256 `ec25f931…26b40`），并生成 `DEPLOYED.sha256`。
- `./rkav-gateway --validate-config`：**EXIT=0**，配置合法。
- 运行：`./run_retest.sh`（记录运行前 dmesg/时间与退出码脚本），10 秒窗口内网关实际运行
  约 2.9 秒后因致命错误停止，**退出码 1**。

## 5. 失败细节与证据

- 应用日志首错（`run.log`，02:57:45.515）：
  `mpp_rga_video_encoder.rga_convert: RGA RGB/BGR to NV12 conversion failed
  [category=codec, native_code=0, retryable=false]`；
  随后 `mpp_rga_video_encoder.flush: MPP did not emit end-of-stream within two seconds`，
  管道以 `fatal_error` 停止。
- RGA 用户库诊断输出：`RgaBlit(1356) RGA_BLIT fail: Bad address`；
  源包装为 `fd=0, vir=0x7fb2a98010, phy=(nil)`、`rect[0,0,1280,720,3840,720,…]`，
  目标为 `fd=0, vir=0x7fbb8be000, phy=(nil)`、`rect[0,0,1280,720,1280,720,…]`。
- 内核新增日志（dmesg 增量，唯一新增失败组）：
  ```text
  [ 7322.975094] usb 1-1.1: 3:1: cannot get freq at ep 0x84          ← 已知 UAC 警告
  [ 7323.266881] rga2_mmu: RGA2 failed to get pte, result = 1352, pageCount = 2026
  [ 7323.266946] rga2_mmu: rga2 map src0 memory failed
  [ 7323.266960] rga2_reg_init, [798] set mmu info error
  [ 7323.266972] rga2: init reg fail
  ```
- 业务计数：视频采集 2、音频块 1、推理请求 1、推理结果 0、视频包 0、错误 2、恢复 0；
  H.264 输出 0 字节（`.part`），零残留进程。
- 证据归档：板端目录内 `failure-evidence.sha256` 6/6 `OK`；Windows 侧
  `D:\share\mpp-retest-20260914-105633-6922\` 为 `adb pull` 副本，本地重算 SHA-256
  与板端清单完全一致（含 `run.log` `75d1dfc9…bd8c`、`dmesg.before` `84b4f45d…a628`、
  `dmesg.after` `59a7ef3b…516e`）。

## 6. 根因分析与当前假设

1. **已确认**：MPP 配置键拼写修复有效，`configure_encoder` 阶段不再是瓶颈（P115 关闭）。
2. **已确认**：失败发生在 RGA 把 CPU 堆上的 RGB/BGR 帧转换为 NV12 的第一次 `RgaBlit`，
   且卡在 **src0 地址解析**，不是 MPP 编码、不是摄像头、不是模型。
3. **当前假设（待修复验证）**：板端 RGA 运行时为 `librga 1.3.2_[0]`，内核驱动为
   `rga2 ver:3.2.63318`（4.19 BSP），两者都早于“虚拟地址直接可用”的现代组合。
   项目代码用 `wrapbuffer_virtualaddr` 包装两块内存：源是解码器 `Buffer`（普通堆内存），
   目标是 MPP ION 缓冲区经 `mpp_buffer_get_ptr` 映射的虚拟地址。内核在把 src0 “虚拟地址”
   映射进 RGA MMU 时取 PTE 失败，说明旧驱动/旧库路径无法为堆虚拟地址建立 DMA 映射。
   板端库已导出 `wrapbuffer_fd_t`，MPP 已导出 `mpp_buffer_get_fd_with_caller`，fd→dma-buf
   是这套旧运行时支持的标准路径。
4. **排除依据**：`wrapbuffer_virtualaddr_t`/`imcvtcolor_t` 符号存在且调用到达内核，说明不是
   符号缺失或配置校验问题；dmesg 只有这一组新增 RGA 失败日志。

## 7. 下一步（按固定顺序）

1. **修复候选 v2（fd 路径）**：把 `MppRgaVideoEncoder` 的转换改为 fd 包装——目标继续用
   MPP ION 缓冲区的 fd；源新增一个 MPP ION RGB 缓冲区，逐帧拷贝 RGB 后取 fd，两侧都用
   `wrapbuffer_fd`；不改变编码参数、队列与生命周期语义。
2. **重建与门禁**：Ubuntu 22.04 容器、GCC 11.4、AArch64；沿用 MPP 1.0.4 头文件与
   `RK356X_Linux_V1.3.2` 兼容约束；产物做 GLIBC ≤2.34、动态依赖和 `strings` 检查。
3. **板端复测**：仍先只读门禁、唯一结果目录、唯一 10 秒实例；通过条件沿用 docs/31 第 4 节
   （退出码 0；H.264 Annex-B 非空且含 IDR；错误/恢复/丢弃/残留为 0；无新增异常内核日志）。
4. 短测通过后才进入板端 RTSP 与恢复、ZLMediaKit 阶段 2、systemd 与 2/12 小时长稳。
5. 已知代价：fd 路径第一版需要对每帧 RGB 做一次 CPU 拷贝（1280x720x3 约 2.76 MB），
   属于“先正确后优化”；后续可用解码器直出 ION 缓冲消除拷贝，但需另行验证。

## 8. 边界

- 本轮只证明“MPP 配置修复有效”和“RGA 虚拟地址路径在本 BSP 不可用”；不证明 fd 路径可用，
  也不证明 H.264 码流质量、RGA 缩放/色彩正确性。
- 未刷写、未重启、未改板端系统文件；旧失败证据目录与哈希保持锁定。
- `zboot`/5.10 刷写路线维持 docs/62/63 的阻断状态，与本轮无关。

## 9. 涉及名词

- **RGA MMU**：RGA 硬件访问内存前把用户缓冲区映射进自己的地址空间；映射失败即
  `failed to get pte`。
- **dma-buf fd**：内核为共享缓冲区签发的“取件凭证”；RGA/RKNN/MPP 都能直接按 fd 取内存，
  比物理地址安全。
- **wrapbuffer_virtualaddr / wrapbuffer_fd**：librga 把一块内存包装成 RGA 可操作图像句柄的
  两种方式；旧运行时只对 fd/物理地址可靠。
- **ADB over TCP**：原用于安卓调试的通道，板端 Buildroot 也运行 adbd（5555 端口），可直接
  传文件和执行命令，是本轮绕过死掉的 SSH 的板端入口。

## 10. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [MPP 配置键拼写根因定位与修复交接](67-MPP配置键拼写根因定位与修复交接.md)
- [MPP/RGA 候选后端实现与兼容构建交接](28-MPP-RGA候选后端实现与兼容构建交接.md)
- [MPP/RGA 首次板端 ABI 短测失败与下一步交接](30-MPP-RGA首次板端ABI短测失败与下一步交接.md)
- [本次板端准备、ABI 短测与下一步交接](31-本次板端准备、ABI短测与下一步交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md) 与 [通俗版](07-项目问题汇总-通俗版.md)，P122
