# GEC V11 B 方案 DTS 移植（显示/触摸/WiFi）候选构建与门禁交接

更新日期：2026-09-16（2026-09-26 追加：板端发生白屏事故并已救援恢复，见 [81 号交接](81-GEC-V11白屏变砖串口诊断与419原厂系统恢复交接.md)；**本候选仍未刷写、归档完好**，刷写前必须先读 81 号 §0/§7 的新前置门禁）
状态：**离线候选已构建并通过全部离线门禁；未刷写、未触碰板端**。刷写等待用户明确确认。
本文件是 GEC V11 / LubanCat 5.10 路线的最新交接；上一份为 78 号交接（§11/§12）。

## 1. 结论

1. B 方案（把出厂 4.19 DTS 的板级节点移植进 5.10 候选 DTS）已完成离线闭环：
   - 新候选 DTS：`rk3568-gec-v11-display-wlan-touch-candidate.dts`（206 行，v3 有线/串口基线原样保留 + 显示/触摸/WLAN 三段覆盖）；
   - 新 defconfig：`rockchip_rk3568_gec_v11_display_wlan_touch_zboot_defconfig`（与 zboot 候选仅差 `RK_KERNEL_DTS_NAME`）；
   - 产物 `zboot.img` **16,410,112 字节（15.65 MiB，<32 MiB 分区）**，SHA-256
     `407b36fb802d4fe98bfe269e172b1784ea4c66d0848b4891119e8cc68f5fec03`；
   - **内核载荷刻意复用两次真机启动验证过的 `Image.lz4`（`49a27d56…c18807`）**，见 §3 的处置说明；
   - fdt 载荷 = 新 DTB（`2adf405e…a48d`，184,119 字节）；resource = `36f431cd…bbfa`（logo 逐字节不变，内嵌 `rk-kernel.dtb` 更新为新 DTB）。
2. 候选包：`rkav-gec-v11-display-wlan-touch-candidate-20260916.tar.gz`
   （13,333,374 字节，SHA-256 `10e1382cd0c393c7e784a4988e4c4605b03243f4d0ee9f6f8ba08d64b80aca4c`），
   已归档 VM `/home/china/`、Windows `D:\share\`、`C:\Users\CC\Downloads\` 三处。
3. **未刷写**：boot 分区仍为 4.19 备份镜像；板端只读门禁确认 4.19.232 在位、rootfs 可写、
   备份完整（`/userdata/rkav/gec-backup-20260914`：boot-p3 32MiB + misc-p2 4MiB + partitions.txt）、
   无 rkav-gateway 残留进程。

## 2. 节点移植清单（出厂 4.19 DTS → 5.10 候选）

| 板级项 | 出厂 4.19 配置（逐项核对） | 5.10 候选处理 |
|---|---|---|
| 显示路由 | DSI0→VP0（route-dsi0 connect=vop port@0 endpoint@0），HDMI 独立 | `&dsi0_in_vp0` okay / `&dsi0_in_vp1` disabled / `&route_dsi0{status=okay;connect=<&vp0_out_dsi0>}`；`&route_edp` disabled（EVB1 原本在 vp0 启用 EDP 路由，已关闭） |
| HDMI 翻转 | factory HDMI→VP1 | `&hdmi_in_vp0` disabled / `&hdmi_in_vp1` okay / `&route_hdmi{connect=<&vp1_out_hdmi>}`（EVB1 原本 HDMI 在 vp0，已翻转） |
| 面板 | `panel@0`：simple-panel-dsi、1024x600@51.2MHz（hfp160/hs2/hbp160/vfp12/vs2/vbp23）、reset=GPIO3_A7 低有效、delay 60ms、flags 0xa03、RGB888、4 lane、init-sequence 333 字节（20 条命令） | `&dsi0_panel`：删 width-mm/height-mm、reset-gpios 覆写、init-sequence **逐字节拷贝**（脚本校验 20 条三元组、333 字节）；`&dsi0_timing0` 覆写全部时序字段 |
| 面板供电 | `vcc3v3-lcd0-n`：GPIO0_C7 使能（active-high）+ vcc3v3_sys | EVB1 基线 GPIO0_C7 与出厂**恰好一致**（0x3e/0x17 交叉核对成立），仅补 `vin-supply=<&vcc3v3_sys>` |
| 背光 | pwm@fe6e0000（=5.10 `pwm4`，GPIO0_C3 mux1，与 EVB 同引脚）、周期 1,000,000ns、默认亮度 20 | `&backlight{pwms=<&pwm4 0 1000000 0>;default-brightness-level=<20>}`；pwm4 引脚复用无需改 |
| 触摸 | `i2c1(fe5a0000)` 上 `gt911@5d`：IRQ GPIO3_B3 下降沿、reset GPIO3_B4、reg 0x5d | `&i2c1` 内 `/delete-node/ gt1x@14`（EVB 触摸）+ `gt911@5d`（irq-gpios/reset-gpios）；**修正 78 号交接"I2C1@0x14"的假设——出厂权威值是 0x5d** |
| WiFi 控制器 | sdmmc1(fe2c0000)：supports-sdio、bus4、SDR104、non-removable、mmc-pwrseq、150MHz | `&sdmmc1` status okay；`supports-sdio` 在 5.10 dw_mmc 驱动中无解析（grep 证据），改用 EVB1 sdio 属性组（no-sd/no-mmc/cap-sdio-irq/non-removable/keep-power-in-suspend/sdr104） |
| WiFi 上电 | sdio-pwrseq：reset GPIO2_C4 低有效、pinctrl wifi_enable_h（GPIO2_C4 pull-none）、post-power-on-delay 200ms、无时钟 | `&sdio_pwrseq` /delete-property/ clocks、clock-names（删除 EVB rk809 ext_clock）；reset-gpios=<&gpio2 RK_PC4 GPIO_ACTIVE_LOW>；`&wifi_enable_h` pins 覆写 |
| WiFi 平台节点 | wireless-wlan：wlan-platdata、wifi_chip_type=rtl8723ds、WIFI,host_wake_irq=GPIO2_C3、pinctrl（GPIO2_C3 pull-down）；无 poweren_gpio | `&wireless_wlan`：/delete-property/ WIFI,poweren_gpio（EVB ap6398s 遗留 GPIO3_D5 必须删除）、wifi_chip_type=rtl8723ds、host_wake=GPIO2_C3、pinctrl-0=<&wifi_host_wake_irq>；pinctrl 组覆写（pull_down=0x140 与出厂 0x130 同义，pull_none=0x137 与出厂 0x127 同） |
| 蓝牙 | wireless-bluetooth（RTL8723DS 蓝牙侧） | 维持 v3 禁用（本轮范围仅 WLAN/触摸/屏） |

与出厂 DTS 的两处已知偏离（均写入候选 DTS 头注释）：
- `irq-gpios` 的 0x04（push-pull）在 5.10 `include/dt-bindings/gpio/gpio.h` 无对应宏，改用 GPIO_ACTIVE_HIGH（输入引脚，无功能影响）；
- `supports-sdio` 属性替换为 EVB1 的 SDIO 属性组（见上表）。

## 3. 内核载荷哈希漂移的定位与处置（重要）

今天 `./build.sh kernel` 重新生成了 `Image.lz4`（`ba684bfc…7cce`，15,993,043 字节），与两次真机
启动验证过的 zboot 候选载荷 `49a27d56…c18807`（15,993,042 字节）不一致。处置：

1. 解压对比两份 Image：大小相同（37,102,080 字节），**仅 69 字节差异且集中于内核版本横幅**
   （`#8 SMP Thu Sep 3 16:40:37 CST 2026` → `#9 SMP Wed Sep 16 09:59:19 CST 2026`，工具链一致；
   `cmp -l` 全文件仅 69 处差异）；`.config` 由构建重新生成，`CONFIG_TOUCHSCREEN_GOODIX is not set`
   （9/15 模块构建的临时片段未进入本次配置）。
2. 结论：差异为构建横幅/时间戳级别，但按"一次只变一个变量"纪律，**弃用今日 Image.lz4**，
   使用归档的 `evidence/zboot-candidate-20260914/kernel-payload.lz4`（`49a27d56…`）重打 FIT——
   刷板后的内核与已真机验证的完全一致，唯一变量是 DTB。
3. 重打 FIT 用 SDK 打包链：`mk-fitimage.sh zboot.img device/rockchip/.chip/boot.its
   <kernel-payload.lz4> <新DTB> <resource.img>`；mkimage 汇报三载荷哈希与预期逐一相符（§4）。
   SDK 自产的中间产物（kernel 载荷为今日 Image）留档 `zboot-img-sdkbuild-20260916.img`
   （`d02e7fc3…`），**不用于刷写**。
4. resource.img（`36f431cd…`）与上次候选（`7f8edb4a…`）差异定位：解包逐文件比对，logo.bmp 与
   logo_kernel.bmp 两份**逐字节一致**（`c30c01f0…`），唯一差异是内嵌 `rk-kernel.dtb` 更新为新 DTB
   ——符合预期且与 fdt 载荷保持一致。

## 4. 离线门禁（全部通过）

| 门禁 | 结果 |
|---|---|
| SDK 构建链 | `build.sh rk3566_rk3568:<新defconfig>` CONFIG_EXIT=0；`build.sh kernel` KERNEL_EXIT=0（日志在 evidence 目录） |
| DTB 反编译 | dtc -I dtb -O dts 退出码 0（告警与既有候选同类：vendor DTS 固有噪声） |
| 结构断言（反编译文本逐项核对） | model=GEC V11；dsi0 status=okay；panel@0 compatible/reset-gpios（GPIO3_A7 低有效）/flags 0xa03/4 lane/60ms delay；**init-sequence 与出厂逐字节一致（333 字节、20 条命令）**；timing 51.2MHz/1024x600/porches 全一致；backlight pwms=<pwm4(0x145) 0 1000000 0>+默认亮度 20；pwm@fe6e0000 phandle 闭环（0x145）且 status okay |
| route/endpoint 闭环 | route-dsi0 connect=0x17 == `/vop@fe040000/ports/port@0/endpoint@0`(vp0_out_dsi0) phandle 0x17；dsi0_in_vp0=okay(0x9c↔0x17)、dsi0_in_vp1=disabled；panel↔dsi_out_panel 双向 remote-endpoint 闭合（0xaa↔0xae）；hdmi_in_vp0=disabled、hdmi_in_vp1=okay(0xa3↔0x1a)、route_hdmi connect=0x1a=vp1_out_hdmi |
| 触摸 | i2c1 status=okay；gt911@5d（reg 0x5d、irq GPIO3_B3 下降沿、reset GPIO3_B4）；EVB gt1x@14 已删除 |
| WLAN | sdmmc1 status=okay+SDIO 属性组+pinctrl 三组+mmc-pwrseq=<sdio-pwrseq(0xbb)>；sdio-pwrseq reset=GPIO2_C4 低有效+delay 200ms+无时钟；wifi-enable-h 引脚覆写 GPIO2_C4 pull-none；wifi-host-wake-irq 覆写 GPIO2_C3 pull-down（与出厂 0x130/0x127 pcfg 一致）；wireless-wlan status=okay+rtl8723ds+host_wake GPIO2_C3+poweren 已删除；sdmmc2(fe000000) disabled；wireless-bluetooth disabled |
| FIT 结构 | totalsize 0xfa6600=16,410,112 字节；fdt@0x800（184,119B，哈希=新 DTB）、kernel@0x2d800（15,993,042B，哈希=49a27d56）、resource@0xF6E200（229,376B，哈希=36f431cd）；signature 为无值节点（与可启动 FIT 同构）；mkimage 汇报哈希与独立解包/断言一致 |

## 5. 证据与归档

- VM 证据目录：`/home/china/rkav-gec-v11-sdk-view-20260903-112850/evidence/display-wlan-touch-v4-20260916/`
  （DTS/DTB/反编译/zfit.dts/config.log/kernel.log/SHA256SUMS/MANIFEST.txt/SDK 中间产物）
- 候选包：`rkav-gec-v11-display-wlan-touch-candidate-20260916.tar.gz`
  （VM `/home/china/`、`D:\share\`、`C:\Users\CC\Downloads\`，SHA-256 `10e1382c…aca4c`）
- 新 DTS/defconfig 双写位置：SDK 视图与 `rkav-gec-v11-kernel-candidate-20260903-093839/kernel-5.10`
  （同一工作树的符号链接视图，MD5 一致 `56608e95…`）

## 6. 板端只读门禁（2026-09-16，adb root，未做任何写操作）

| 检查项 | 结果 |
|---|---|
| 当前系统 | `Linux RK356X 4.19.232 #2 SMP Jul 17 2025`（旧系统，未刷入） |
| rootfs | `/` ext4 **rw**（可替换模块）；oem/userdata 均挂载 rw |
| 待替换模块 | `/system/lib/modules/`：`8723ds.ko`（4,810,208 字节，4.19）、`goodix.ko`（24,424 字节，4.19） |
| 回滚备份 | `/userdata/rkav/gec-backup-20260914/`：`boot-p3-backup-32MiB.bin` + `misc-p2-backup-4MiB.bin` + partitions.txt，三处留档哈希一致 |
| 业务进程 | `pidof rkav-gateway` 为空 |

## 7. 刷写与验收计划（等待用户明确确认后执行）

1. 传输：`adb push` 候选包到 `/userdata/rkav/` 并复核 SHA-256；
2. 写入：仅写 boot 分区 `dd if=zboot.img of=/dev/block/by-name/boot bs=1M conv=fsync`
   （16,410,112 字节写入 32 MiB 分区；不触碰 uboot/misc/rootfs/recovery），读回哈希比对；
3. 模块替换（rootfs 可写、有备份）：备份 `/system/lib/modules/` 两个 4.19 模块到
   `/userdata/rkav/`，用 5.10 的 `RTL8723DS.ko`（4,207,736 字节，`dd1379e9…`）与 `goodix.ko`
   （528,240 字节，`ebd6530e…`）替换同名文件（`8723ds.ko` 按板端现有小写文件名放置），
   哈希与三处留档核对；
4. 重启验收：内核 5.10.209；屏亮（演示动画/logo）；触摸 `evtest` 出坐标；`wlan0` 出现并可扫描/连网；
   GMAC1 网口仍为 `eth0` 192.168.50.2（v3 结论回归）；证据入唯一结果目录；
5. 回滚：任一步失败用 32MiB 备份 `dd` 回写 p3 重启回 4.19；
6. 风险：写盘期间掉电可能损坏 boot 分区（有备份）；屏/触摸/WiFi 为新增变量，首启不带负载、
   不启动网关/NPU/MPP/RTSP。

## 8. 边界

- 本轮只证明"B 方案 DTS 候选离线构建与结构门禁通过"，不证明 5.10 下屏/触摸/WiFi 真机可用；
- 内核 Image 与上次真机启动完全一致（§3），模块 insmod 行为已在 78 号交接 §12 验证；
- U-Boot 触发机制（bootdelay/BCB/寄存器）维持 P114/78 号交接的证伪结论，不再尝试。

## 9. 名词

- **B 方案 DTS 移植**：把出厂 4.19 设备树的板级节点搬到 5.10 候选 DTS，让 5.10 内核"认识"板上硬件；
- **vp0/vp1**：RK3568 显示控制器 VOP 的两个视频端口，DSI/HDMI/EDP 各自挂在某一路；
- **sdio-pwrseq**：SDIO WiFi 的上电时序控制（REG_ON 引脚）；**wlan-platdata**：瑞芯微 WiFi 平台驱动，
  读 `wifi_chip_type` 供驱动识别芯片型号；
- **resource.img**：RK 固件资源包（logo 图片 + 内嵌当前 DTB），U-Boot 随 FIT 一并提供。

## 10. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [GEC V11 压缩启动候选与 U-Boot 格式核验交接](78-GEC-V11压缩启动候选与U-Boot格式核验交接.md)（§11 首次启动、§12 模块验证）
- [本次会话实现总结、问题归档与下一步交接](79-本次会话实现总结、问题归档与下一步交接.md)
- [项目问题汇总：面试版](06-项目问题汇总-面试版.md) 与 [通俗版](07-项目问题汇总-通俗版.md)，P114、P125、P126
