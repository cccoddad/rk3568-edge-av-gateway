# GEC V11 候选 v3 创建与 SDK 配置切换交接

更新日期：2026-09-03

## 1. 候选内容

隔离 kernel/device worktree 已新增：

- `rk3568-gec-v11-wired-serial-candidate.dts`，SHA-256
  `817da7066221a666166e87bbd16ff91c4987bb0b0b7cc38beabf5c3276cad3ae`；
- `rockchip_rk3568_gec_v11_wired_serial_candidate_defconfig`，SHA-256
  `f1eb11d5906503d4af11e36635801a66f74cac6d45bb935f54f2c672b7f24994`。

v3 继承 v2 的 GMAC1 和 IO 电源域修正，只增加：

```text
&uart0 { status = "okay"; };
&uart1 { status = "okay"; };
&sdmmc2 { status = "disabled"; };
&wireless_wlan { status = "disabled"; };
&wireless_bluetooth { status = "disabled"; };
```

这是保守的有线/恢复候选，不声称 GEC RTL8723DS Wi-Fi 已移植。

## 2. 配置门禁

使用 `rk3566_rk3568:rockchip_rk3568_gec_v11_wired_serial_candidate_defconfig` 切换成功，
`output/.config` 中确认：

```text
RK_DEFCONFIG="rockchip_rk3568_gec_v11_wired_serial_candidate_defconfig"
RK_KERNEL_DTS_NAME="rk3568-gec-v11-wired-serial-candidate"
```

## 3. 当前状态与下一步

候选 v3 尚未编译；原 SDK 工作树保持干净，隔离树仅新增候选文件，未部署、未刷写、未操作开发板。
下一步使用已验证 host shim 和 `-j2` 执行增量 kernel 构建，再反编译确认串口和无线节点状态。
