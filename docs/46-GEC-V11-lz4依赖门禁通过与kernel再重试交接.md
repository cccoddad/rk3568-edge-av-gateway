# GEC V11 lz4 依赖门禁通过与 kernel 再重试交接

更新日期：2026-09-03

## 1. SDK 检查条件

`check-kernel.sh` 通过 `lz4 -h` 是否含 `favor-decSpeed` 判断主机 lz4 是否满足要求；提示文字建议至少
使用 v1.9.4。

## 2. Ubuntu 实际状态

本次检查确认：

```text
command=/usr/bin/lz4
package=1.9.4-1build1.1
runtime=v1.9.4
install_exit=0
LZ4_GATE=PASS
```

`apt-get install -y lz4` 显示该包已经是最新版，新增、升级、卸载均为 0，因此本次没有改变 Ubuntu 软件包
集合。`candidate_version` 为空是因为检查命令匹配英文 `Candidate:`，而 apt 当前使用中文“候选：”；不影响
已安装版本和最终门禁。

## 3. 当前边界

- 尚未在依赖门禁通过后重跑 kernel；
- Image、候选 DTB 与 boot 产物仍未生成；
- 原 SDK 未修改，未部署、未刷写、未操作开发板。

## 4. 下一步

沿用固定目录 `host-shims-20260903-140236`，再次执行隔离 SDK normal kernel 构建。验收继续要求实际
`-j2`、日志无错误、候选 DTB/Image/boot 产物存在并通过哈希与反编译属性检查。若出现新的主机依赖缺失，
保留日志和无产物状态后停止。
