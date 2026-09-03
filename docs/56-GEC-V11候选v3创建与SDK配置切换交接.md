# GEC V11 候选 v3 创建与 SDK 配置切换交接

更新日期：2026-09-03

隔离 kernel/device worktree 已新增 `rk3568-gec-v11-wired-serial-candidate.dts`（SHA-256
`817da7066221a666166e87bbd16ff91c4987bb0b0b7cc38beabf5c3276cad3ae`）和
`rockchip_rk3568_gec_v11_wired_serial_candidate_defconfig`（SHA-256
`f1eb11d5906503d4af11e36635801a66f74cac6d45bb935f54f2c672b7f24994`）。

v3 继承 v2 的 GMAC1、IO 电源域修正，仅启用 `uart0/uart1`，并禁用 EVB Wi-Fi 的 `sdmmc2`、
`wireless_wlan`、`wireless_bluetooth`。这是一份保守有线/恢复候选，不声称 RTL8723DS Wi-Fi 已移植。

使用 `rk3566_rk3568:rockchip_rk3568_gec_v11_wired_serial_candidate_defconfig` 切换成功，
`output/.config` 已确认候选 defconfig 和 DTS 名称。v3 尚未编译，原 SDK 工作树保持干净，未部署、未刷写、
未操作开发板。
