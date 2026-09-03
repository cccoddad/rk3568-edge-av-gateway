# GEC V11 候选 v3 DTS 语法失败与最小修正交接

更新日期：2026-09-03

首次执行隔离 SDK `./build.sh kernel` 时，DTC 在
`rk3568-gec-v11-wired-serial-candidate.dts:27.22-28.1` 报 `syntax error`，并返回内部 exit code 2。
错误是 `&wireless_bluetooth` 覆盖块缺少结束符 `};`，属于候选文本生成错误。

日志位于 `evidence/wired-v3-build-20260903-191605/kernel.log`，SHA-256 为
`9797ce6f601f558f769624e21aa6c71435fcb6e4b9c7aabe324562a42f849459`。v3 DTB 未生成；Image、boot 和
firmware 链接仍为 v2 旧产物，`fresh=0`，因此没有误判 v3 成功。

SDK 外层再次错误报告 `build_exit=0`，后续仍以日志和产物为准。下一步只增加缺失的 `};`，先独立 DTC
复测，再用 host shim 和 `-j2` 重走 v3 kernel 构建；原 SDK、v1/v2 候选和开发板均不操作。
