# GEC V11 IO 电源域差异锁定与候选 v2 前门禁交接

更新日期：2026-09-03

## 1. 对比输入与证据

定向对比使用已构建的 5.10 GEC 候选 DTB 与公开 factory 4.19 DTB：

- 候选 DTB SHA-256：`2d6daaff1b56e1eda5b2ac22162be5cc7d0128406f56a5ada45e24380394818a`；
- factory DTB SHA-256：`d793ee4486112e36409ae46594988b5b358d8961bb48243e229736df2293a243`；
- 证据目录：`evidence/io-domain-compare-20260903-165145`；
- 候选/Factory 反编译均返回 0。

候选和 factory 反编译 DTS 的 SHA-256 分别为
`802354bfbb179188f88c93ae6e11230d47acabdb1ec149c710a187d36cd09c16` 和
`076a4213a67c04038444cfca2d43cfeda42172f23acb9392017d094e64d036c5`。

## 2. 已锁定的供电映射

| IO domain | 5.10 当前候选 | 4.19 factory | 结论 |
|---|---|---|---|
| `pmuio2` | `vcc3v3_pmu`，3.3V | `vcc3v3_pmu`，3.3V | 一致 |
| `vccio1` | `vccio_acodec`，3.0V | `vccio_acodec`，3.3V | 电压不同 |
| `vccio3` | `vccio_sd`，1.8–3.3V 可调 | `vccio_sd`，1.8–3.3V 可调 | 一致 |
| `vccio4` | `vcc_3v3`，3.3V | `vcc_1v8`，1.8V | 电压不同 |
| `vccio5` | `vcc_3v3`，3.3V | `vcc_3v3`，3.3V | 一致 |
| `vccio6` | `vcc_3v3`，3.3V | `vcc_1v8`，1.8V | 电压不同 |
| `vccio7` | `vcc_3v3`，3.3V | `vcc_3v3`，3.3V | 一致 |

因此当前候选不能上板。候选 v2 预计需要把 `vccio4/6-supply` 改为 `vcc_1v8`，并把
`vccio_acodec` 固定电压从 3.0V 调整为 factory 的 3.3V；实施前必须先确认 5.10 源码中的 label 和覆盖位置。

## 3. SDK 自动检查器问题

`check-power-domain.sh` 对整个反编译 DTS 使用跨行 `grep -Pzo`，随后通过字符串拼接和 `eval` 追踪
phandle。大型 DTS 上连续 7 次达到 PCRE 回溯上限，所以它的打印结果不能作为完整电源域证明。本次改用
定向节点、phandle、regulator 名称及微伏值对照，证据文本和哈希均已保存。

## 4. 边界与下一步

- 未修改候选 DTS，未重新构建；
- 当前 FIT `boot.img` 明确禁止刷写；
- 未部署、未操作开发板；
- 下一步只读核对 5.10 EVB1 include 链中 `vccio_acodec`、`vcc_1v8`、`vcc_3v3` 和
  `pmu_io_domains` 的 label/定义，再创建候选 v2 并离线构建。该 label 门禁随后已通过，见第 52 号交接。
