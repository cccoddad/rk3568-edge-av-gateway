# GEC V11 电源域覆盖 label 通过与候选 v2 构建交接

更新日期：2026-09-03

## 1. include 链与定义位置

候选 v1 include `rk3568-evb1-ddr4-v10-linux.dts`，后者依次包含
`rk3568-evb1-ddr4-v10.dtsi`、`rk3568.dtsi`、`rk3568-evb.dtsi` 和 `rk3568-linux.dtsi`。

在 `rk3568-evb.dtsi` 中已确认：

- `vccio_acodec` label 位于 RK809 LDO_REG4，当前固定 3000000 微伏；
- `vcc_1v8` label 位于 DCDC_REG5，固定 1800000 微伏；
- `vcc_3v3` label 位于 SWITCH_REG1；
- `&pmu_io_domains` 已配置完整 supply 映射。

精确门禁结果为三个 `FOUND_LABEL`、一个 `FOUND_OVERRIDE pmu_io_domains`、`label_gate=0`。

## 2. 证据

证据目录为 `evidence/io-domain-labels-20260903-165722`：

- 当前候选源码 SHA-256：`631e67fab202ff8c1685855e29cf4277ce84f89033eeded95c36d25b701cafd6`；
- include 链文本：`89b2db7d671dc3434d4df48be39b1bfdea2771e92c51401a472a11b0303ce944`；
- label 命中清单：`bcbefb706aefbb7b21f809b2cdd695620ae9fb9686d05916a205f4a5906c4055`；
- 相关定义上下文：`e6cfeef2766f46b74d953f36035743d8e113f0a2def900a7494ea825d22995e0`。

## 3. 候选 v2 最小改动设计

为保留 v1 证据且避免修改公共 EVB DTSI，候选 v2 使用新文件名并 include v1，仅覆盖：

```text
vccio_acodec: 3000000 -> 3300000 微伏
vccio4-supply: vcc_3v3 -> vcc_1v8
vccio6-supply: vcc_3v3 -> vcc_1v8
```

同时创建独立 SDK defconfig 指向 v2。构建后必须使用 DTB `/__symbols__` 与 `fdtget` 比较实际 phandle，确认
`vccio4/6` 都引用 `vcc_1v8`，并确认 `vccio_acodec` 最小/最大电压都是 3300000。

## 4. 当前边界

本阶段未修改候选、未重新构建；v1 FIT 仍禁止刷写。原 SDK 未修改，未部署、未操作开发板。下一步创建并
构建候选 v2；即使 v2 主机门禁通过，也仍需继续核对其他板级差异，不能立即刷板。
