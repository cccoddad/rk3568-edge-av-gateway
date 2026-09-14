# GEC V11 5.10 压缩启动候选与 U-Boot 格式核验交接

更新日期：2026-09-14
状态：**压缩启动候选已构建并通过离线门禁；U-Boot 格式兼容性已由真实板端只读证据确认；旧 boot
分区已备份；刷写等待用户明确确认（本轮未写盘、未重启）**。本文件是 GEC V11 / LubanCat 5.10
路线的最新交接。

## 1. 结论

1. **真实分区容量修正**：板端 eMMC `boot`(p3)、`recovery`(p4)、`backup`(p5) 各为 **32 MiB**
   （65536×512B，sysfs 实测）。此前 docs/62/63 记录的 16 MiB 不正确（见 P125）。未压缩 v3 FIT
   为 35.78 MiB，无论按 16 MiB 还是 32 MiB 都放不下，压缩路线依然必要。
2. **U-Boot 兼容性核验通过**：真实板端 U-Boot 的 `bootcmd` 为
   `boot_android ${devtype} ${devnum};boot_fit;bootrkp;run distro_bootcmd;`，实际启动走 `boot_fit`；
   U-Boot 二进制含 `lz4 compressed`、`gzip compressed`、`lzo compressed` 与 inflate 字符串，
   支持 LZ4/GZIP/LZO 解压；当前 boot 分区内容为 FIT（magic `d0 0d fe ed`），总大小 22,581,760
   字节，内核 21.24 MB、`compression=none`；FIT 结构含 `sha256,rsa2048` 签名节点但**没有签名值**
   （未签名），说明 FIT 验签未强制执行。
3. **压缩候选已构建**：在隔离 SDK 视图新增 `RK_BOOT_COMPRESSED=y` 候选 defconfig，走 SDK 的
   `zboot.its`（内核 `compression="lz4"`）模板生成 `zboot.img`：
   - 大小 **16,412,160 字节（15.65 MiB）**，小于 16 MiB（保守门禁）与 32 MiB（实际分区）；
   - 结构：FIT，fdt 0x800/0x2D2EF、kernel 0x2DC00/0xF408D2（lz4）、resource 0xF6E600/0x38400，
     `totalsize=0xFA6E00`；签名为无值节点，与板端可启动的旧 FIT 同构；
   - 载荷字节级比对通过：内核载荷 = `Image.lz4`（SHA-256 `49a27d56…c18807`）、fdt = v3 候选
     DTB（`0a89ede6…d0f8f2`）、resource = `resource.img`（`7f8edb4a…bfe40ee`）；
   - 产物 SHA-256：`d133fd00c15ed8832bd89a8fc2140ddb27f5929d1f8219b94f523be9d47ce549`。
4. **安全准备**：已只读备份旧 boot 分区 32 MiB 到板端 `/userdata/rkav/gec-backup-20260914/`，
   SHA-256 `26a5758cdc88bdd2920ba2ffe309f73ec5f84a4374db634dd8418712462f1033`；Windows
   `D:\share\gec-backup-20260914\` 与 Ubuntu VM 各留一份，哈希一致，具备 `dd` 回滚条件。
5. **未执行**：没有写任何块设备、没有刷写、没有重启；`.part` 状态未产生。

## 2. 只读板端证据（2026-09-14，adb root）

| 项目 | 结果 |
|---|---|
| 分区（/proc/partitions + sysfs 扇区） | p1 uboot 8192、p2 misc 8192、p3 boot 65536、p4 recovery 65536、p5 backup 65536、p6 rootfs 12582912、p7 oem 262144、p8 logo 32768、p9 userdata 17670111（单位：512B 扇区） |
| 当前系统 | Linux 4.19.232，`root=PARTUUID=614e0000-0000`，verity 状态 orange（解锁） |
| boot 分区头部 | `d0 0d fe ed`（FIT/FDT magic）；totalsize 0x1589E00；fdt 0x800/0x218A7、kernel 0x22200/0x153C808（`compression=none`）、resource 0x155EC00/0x2AE00 |
| U-Boot 能力 | `lz4 compressed` / `gzip compressed` / `lzo compressed` / inflate 字符串；`android_boot_flow`、`ANDROID!`、`boot_fit %s` |
| U-Boot 命令 | `bootcmd=boot_android …;boot_fit;bootrkp;run distro_bootcmd;` |
| 签名 | 旧 FIT 的 `signature` 节点没有 `value`（未签名）；v3 FIT 同样没有；SDK `mk-fitimage.sh` 不传密钥 |
| misc 分区 | 含 `AB0` 槽位元数据（单槽 boot/recovery，未使用 A/B） |
| dmesg | 无 U-Boot/FIT 相关错误；设备枚举正常（详见 4.19 现状文档） |

证据文件（板端 `/userdata/rkav/gec-readonly-20260914/`）：
`boot-head-64k.bin`（SHA-256 `82b36c3f…012753`）、`partitions` 快照；解析结果在
`D:\share\gec-readonly-20260914\` 与 Ubuntu VM 的 `fit-parse/`、`v3-fit-parse/`。

## 3. 主机侧构建与门禁（隔离 SDK 视图）

- 视图：`/home/china/rkav-gec-v11-sdk-view-20260903-112850`（含 host shims：`nproc→1`、`python→python3`）；
- 新增候选 defconfig：`rockchip_rk3568_gec_v11_wired_serial_candidate_zboot_defconfig`
  （在原候选基础上仅增加 `RK_BOOT_COMPRESSED=y`）；
- 构建命令：`./build.sh rk3566_rk3568:…_zboot_defconfig` 然后 `./build.sh kernel`，
  日志 `evidence/zboot-candidate-20260914/build.log`；
- SDK 打包链：`mk-fitimage.sh kernel/zboot.img device/rockchip/.chip/boot.its Image.lz4 <dtb> resource.img`
  → `mkimage -f -E -p 0x800`，并把结果 `ln -rsf` 到 `output/firmware/boot.img`；
- 离线门禁：FIT 解析、载荷字节级哈希、大小上限（16 MiB 保守与 32 MiB 实际）全部通过；
- 注意：本次构建同时重打包了未压缩 `kernel/boot.img`（SHA-256 变为 `3123a3c9…`，v3 归档中的
  `a5b893ee…` 仍在 `rkav-gec-v11-wired-v3-20260904-102917.tar.gz` 内保留），部署物只使用
  `zboot.img`，不得混用。

## 4. 产物与归档

- 候选包（WinDownloads 与 VM）：`rkav-gec-v11-zboot-candidate-20260914.tar.gz`，
  13,294,488 字节，SHA-256 `0cf61ff3ce0c4bb500da90fa8d7e474e23bda11c1fd55ca62b6b673e5e531f38`；
  内含 `zboot.img`（`d133fd00…`）、候选 DTB、`zboot-fit.dts`、`build.log`、`MANIFEST.txt`、`SHA256SUMS`；
- 回滚备份：`boot-p3-backup-32MiB.bin`（`26a5758c…`，板端 / Windows / VM 三处哈希一致）。

## 5. 刷写计划（待用户明确确认后才执行）

1. 传输：`adb push` 候选包到板端 `/userdata/rkav/` 并复核 SHA-256；
2. 写入：仅写 `boot` 分区 `dd if=zboot.img of=/dev/block/by-name/boot bs=1M conv=fsync`
   （32 MiB 分区，写入 16,412,160 字节；不触碰 uboot/misc/rootfs/recovery）；
3. 首次启动：只观察串口与内核日志，确认 eMMC/TF、USB、GMAC1、串口、rootfs 挂载；**不启动
   rkav-gateway、NPU、MPP/RGA 或高负载业务**；
4. 回滚：若无法启动或挂载失败，用备份 `dd` 回写 p3 并重启回 4.19；
5. 风险：写盘期间掉电可能损坏 boot 分区（有备份可恢复）；新内核配旧 4.19 rootfs 存在兼容未知；
   串口（COM5）当前未连接，首次启动观察前需要接上；若失败且串口不可用，需通过 TF 卡或
   Maskrom 恢复。

## 6. 边界

- 本轮只证明“压缩装配格式与板端 U-Boot 匹配、候选已冒烟级门禁”，不证明 5.10 能启动、不证明
  驱动/rootfs/媒体链路可用；
- 未刷写、未重启、未修改板端系统（除 `/userdata` 证据与备份文件）；
- 旧 4.19 rootfs 与 5.10 内核、MPP/RGA/RKNN 用户态兼容性仍需启动后逐项核验。

## 7. 下一步

1. 取得用户对“写入 p3 + 重启”的明确确认，并连接串口（COM5）；
2. 执行写入与首次启动观察，保留串口与 dmesg 证据；
3. 启动成功后按 docs/64 第 5 条逐项核验驱动/用户态兼容性，再决定是否纳入主线。

## 8. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [GEC V11 压缩 zboot 尺寸可行与 U-Boot 格式核验交接](63-GEC-V11压缩zboot尺寸可行与U-Boot格式核验交接.md)
- [GEC V11 boot/recovery 容量门禁与刷写阻断交接](62-GEC-V11boot-recovery容量门禁与刷写阻断交接.md)
- [本次 5.10 迁移评估总结、问题归档与下一段任务交接](64-本次5.10迁移评估总结、问题归档与下一段任务交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md) 与 [通俗版](07-项目问题汇总-通俗版.md)，P114、P125
