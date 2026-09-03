# GEC V11 normal kernel 构建通过与 IO 电源域门禁交接

更新日期：2026-09-03

## 1. normal kernel 构建结果

隔离 SDK 视图 `/home/china/rkav-gec-v11-sdk-view-20260903-112850` 使用固定 host shim，以实际 `-j2`
执行 `rk3568-gec-v11-gmac1-candidate.img`，SDK 报告 `build_kernel succeeded`，最终
`FINAL_KERNEL_GATE=PASS`。

本次证据目录为 `evidence/sdk-kernel-final-20260903-161242`，构建日志 SHA-256 为
`b8f3cefcc28c9912634ea77d1230ba745f9b7d66e266a2097213a37098c0c711`。

## 2. 本次新产物

| 产物 | 大小 | SHA-256 |
|---|---:|---|
| kernel `Image` | 37102080 | `e66dca717b321e0c8c4b74aa67786cc97d3c642de7896af14eb6b4b7e9543b86` |
| GEC 候选 DTB | 185067 | `2d6daaff1b56e1eda5b2ac22162be5cc7d0128406f56a5ada45e24380394818a` |
| kernel FIT `boot.img` | 37520896 | `c0e91cb222b51e4bbbc2209ae6e99d8fe7cdfa0f9e9e273f746e6230d73b2569` |

`output/firmware/boot.img` 是指向 kernel `boot.img` 的 75 字节符号链接；跟随链接计算的 SHA-256 与上表
FIT boot 相同。所有关键产物时间均晚于本次开始标记，`artifacts_fresh=1`。

FIT 清单内的 fdt、kernel、resource 哈希分别为候选 DTB 哈希、上述 Image 哈希和
`bd68ff0f990541eb3a436a9a45dec376645ae58afb12587aa4de5277f76ef946`。

## 3. 候选 DTB 断言

DTB 反编译返回 0，`property_gate=0`；反编译文本 SHA-256 为
`802354bfbb179188f88c93ae6e11230d47acabdb1ec149c710a187d36cd09c16`。已确认：

- model 为 `Rockchip RK3568 GEC DDR4 V10 Board`；
- compatible 含 `rockchip,rk3568-gec-v11`；
- GMAC1 使用 input clock、GPIO3 PB5 低有效复位、`tx_delay=0x41`、`rx_delay=0x1e`。

## 4. 尚未通过的上板安全门禁

SDK 构建末尾明确要求检查候选 DTS 的 `pmu_io_domains`，重点为 Wi-Fi、Flash、Ethernet IO 电压域。同时
自动检查连续 7 次报告 PCRE 回溯上限，故不能认为自动检查已完整覆盖。

IO 电源域决定相关引脚 bank 使用 1.8V 或 3.3V。候选目前继承 EVB1 基线，尚未证明与 GEC V11 电气连接
一致。下一步必须定向比较 4.19 factory DTS、5.10 EVB1 和候选的 `pmu_io_domains` 及 regulator 连接；
确认前禁止刷板。

## 5. 工作树与能力边界

- 原 SDK device 工作树为空；隔离 device 仅有候选 defconfig；
- candidate kernel 除候选 DTS 外，正常构建更新了 tracked `.version`，该变化保留、不回滚；
- 未部署、未刷写、未操作开发板；
- 本次证明的是 5.10 候选 kernel/DTB/FIT 可由 SDK 正常构建，不证明 GEC 整板可安全启动，也不证明
  摄像头、音频、NPU、RGA、MPP、H.264、MP4 或 RTSP 已在 5.10 上验证。
