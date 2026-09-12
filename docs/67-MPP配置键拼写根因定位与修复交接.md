# MPP 配置键拼写根因定位与修复交接

更新日期：2026-09-12

## 1. 当前结论

首次板端 MPP/RGA ABI 短测在 MPP H.264 配置阶段返回 `-1` 的直接原因已经定位：板端
`RK356X_Linux_V1.3.2` 运行库只接受历史拼写 `rc:fps_in_denorm` / `rc:fps_out_denorm`，而项目代码当时使用
的 `rc:fps_in_denom` / `rc:fps_out_denom` 直到 MPP 1.0.5（2024-04）才加入配置键表。旧运行库对第一个未知键
返回 `MPP_NOK(-1)`，链式配置随即中止，日志表现为
`configure_encoder ... MPP rejected H.264 encoder configuration, native_code=-1`，与失败目录中的首错完全
一致。

代码已改用历史拼写，并增加配置步骤失败记录。修复后的 AArch64 候选已生成并暂存，等待板端复测；本次没有
刷写、部署或启动板端网关，也没有修改任何板端系统文件。

## 2. 根因证据链

1. **板端唯一失败证据**（保持锁定，不重跑）：`/userdata/rkav/mpp-rga-abi-19700101-021545-755`，
   首个应用错误为 `configure_encoder` 返回 `native_code=-1`，业务计数全零，未生成 H.264。
2. **上游键表核对**：对 2022-11-16 上游提交 `cf5b3571` 的 `mpp/base/mpp_enc_cfg.cpp` 与 MPP 1.0.4 tag
   做只读比对，两代都只有 `rc:fps_in_denorm` / `rc:fps_out_denorm`；`rc:fps_in_denom` 从 1.0.5 起才作为
   别名加入，develop 分支仍保留两个拼写。
3. **原生探针复现**：把 MPP 1.0.4 的配置服务在 Ubuntu 24.04 上以 x86_64 原生编译成最小可执行体（只取
   `mpp_enc_cfg.cpp`、`mpp_cfg.cpp`、`mpp_trie.cpp` 与所需 osal 文件，不需要硬件），按项目真实键顺序做
   链式探针：

   ```text
   OLD chain (denom): final=-1 failed_at=rc:fps_in_denom
   NEW chain (denorm): final=0
   ```

   同时逐键探针结果：`rc:fps_in_denom`、`rc:fps_out_denom` 返回 `-1`；其余项目在用键
   （`prep:*`、`rc:mode`、`rc:bps_*`、`rc:gop`、`codec:type` 等）全部返回 0。
4. **制品核对**：修复前候选二进制 `strings` 含 `rc:fps_in_denom`；修复后候选只含 `rc:fps_in_denorm`。

## 3. 代码修改

文件：`src/media/mpp/mpp_rga_video_encoder.cpp`

- 两个帧率分母键改为历史拼写 `rc:fps_in_denorm` / `rc:fps_out_denorm`，与板端旧库以及 1.0.5 之后所有
  版本兼容。
- 把 15 个 `config_set_s32` 调用收敛到 `set_config` 闭包，记录第一个失败步骤（键名或
  `MPP_ENC_SET_CFG` / `MPP_ENC_SET_HEADER_MODE`）；错误信息从原来的固定文本改为
  `MPP rejected H.264 encoder configuration at <step>`，后续板端复测若仍失败可直接定位。
- 未添加 `prep:range` 或其他未经板端证据支持的键；未改变编码参数、队列或生命周期语义。

## 4. 修复后候选

| 项目 | 值 |
|---|---|
| 构建环境 | Ubuntu 22.04 容器，GCC/G++ 11.4，AArch64 |
| MPP 头文件 | 1.0.4 tag（`rk_mpi.h` SHA-256 `21b6cb2a...24e93`、`rk_venc_cfg.h` SHA-256 `89a41c8d...accdc`） |
| 源码归档 | `rkav-mpp-denorm-fix-source-20260912-1522.tar.gz`，SHA-256 `a4586766...30fd47` |
| 构建结果 | 40/40 通过，最高 GLIBC 2.34（低于板端 2.35 上限） |
| ELF SHA-256 | `afaafe6733d287d7519c93877494108a514eabd7128f7778473ce7c580bf9acd` |
| 封装位置 | WinDownloads `rkav-mpp-denorm-fix-candidate-20260912-152418-8344` |
| 构建日志 | 控制器目录 `container-result-20260912-152241-denormfix` |

候选目录含 `rkav-gateway`、`rk3568-rknn-mjpeg-alsa-mpp-h264.json`、`FIX_INPUTS.provenance` 和
`SHA256SUMS`（已校验）。

## 5. 板端复测建议（需硬件在场并另行确认）

1. 先只读确认板端无遗留 `rkav-gateway`、摄像头/麦克风/NPU 空闲，再部署候选和配置到新的唯一结果目录。
2. 运行 10 秒三硬件 + MPP/RGA 测试；成功判据：生成非空 H.264 elementary stream、业务计数守恒、
   错误/恢复/队列丢弃为零、无新增 MPP/RGA/IOMMU/NPU 内核异常。
3. 若仍失败，先读取新的失败步骤名，再对照旧运行库键表继续定位，不做无证据的键名猜测。

## 6. 边界与未完成项

- 修复只解决配置键拼写；板端编码参数合法性、RGA 色彩路径、H.264 码流质量均未在板端复测。
- 公开的 MPP 1.0.4 与板端 `RK356X_Linux_V1.3.2` 仍是相近但不同代的两个版本；结构体布局和键表已用
  只读比对确认无本项目使用差异，但不能替代板端实测。
- 主机侧原生探针只验证配置服务，不覆盖内核对齐、DMA 缓冲和硬件编码通路。

## 7. 涉及名词

- **配置键表**：MPP 运行时用字符串名注册可配置项（如 `rc:gop`），未知键直接返回错误而不是忽略。
- **MPP_NOK**：MPP 通用失败码，数值为 `-1`；板端日志中的 `native_code=-1` 即来自这里。
- **原生探针**：把目标库的一部分在 x86_64 主机上直接编译并调用，用真实代码验证字符串键行为，
  不需要板端或硬件。
- **历史拼写兼容**：旧键名 `denorm` 从 2022 年到 develop 分支一直保留；新键名 `denom` 是 1.0.5 之后
  追加的别名。选择两者都接受的写法即可跨代兼容。

## 8. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [MPP/RGA 运行时盘点与 SDK 门禁](27-MPP-RGA运行时盘点与SDK门禁.md)
- [MPP/RGA 候选后端实现与兼容构建交接](28-MPP-RGA候选后端实现与兼容构建交接.md)
- [MPP/RGA 首次板端 ABI 短测失败与下一步交接](30-MPP-RGA首次板端ABI短测失败与下一步交接.md)
- [本次板端准备、ABI 短测与下一步交接](31-本次板端准备、ABI短测与下一步交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md) 与 [通俗版](07-项目问题汇总-通俗版.md)，P115
