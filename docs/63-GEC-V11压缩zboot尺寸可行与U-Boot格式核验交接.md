# GEC V11 压缩 zboot 尺寸可行与 U-Boot 格式核验交接

更新日期：2026-09-04

## 1. 主机评估

在隔离 candidate kernel worktree 中执行 `make -j2 Image.lz4 zboot.img` 返回 0。SDK 当前配置未设置
`RK_BOOT_COMPRESSED`，所以本次只生成尺寸/格式候选，没有修改 SDK 配置或完整重打包。

## 2. 尺寸与格式

板端 boot 分区容量为 16777216 字节（16 MiB）：

| 产物 | 大小 | 是否放入 16 MiB |
|---|---:|---|
| `Image` | 37102080 | 否 |
| `Image.lz4` | 15993042 | 是，余 784174 字节 |
| `zboot.img` | 16228352 | 是，余 548864 字节 |
| 未压缩 `boot.img` | 37520896 | 否 |

`zboot.img` 被识别为 Android bootimg；其内容包含 GEC v3 compatible 和设备树字符串。SHA-256 为
`b432ba0dcfa2e69fa80a5ae0c89ed9cd34b585769fcdd3be976108b4454cb130`。`Image.lz4` SHA-256 为
`49a27d56aab984da79724e7373952617147fdc8d48f1355ea903039235c18807`。

## 3. 尚未通过的格式门禁

尺寸合适不等于当前 U-Boot 会接受压缩 boot。下一步只读读取板端旧 `/dev/block/by-name/boot` 的 magic、
页大小和内核头部，再决定是否构造 `RK_BOOT_COMPRESSED=y` 候选。

## 4. 边界

未执行 `dd`、升级工具、重启或板端写操作；旧 4.19 系统、未压缩 v3 归档和压缩候选均保留。
