# GEC V11 boot/recovery 容量门禁与刷写阻断交接

更新日期：2026-09-04

## 1. 分区映射

真实板端确认：

```text
/dev/block/by-name/boot     -> /dev/mmcblk0p3   32768 blocks = 16777216 bytes (16 MiB)
/dev/block/by-name/recovery -> /dev/mmcblk0p4   32768 blocks = 16777216 bytes (16 MiB)
/dev/block/by-name/rootfs   -> /dev/mmcblk0p6
/dev/block/by-name/oem      -> /dev/mmcblk0p7
/dev/block/by-name/userdata -> /dev/mmcblk0p9
```

当前系统从 `/dev/mmcblk0p6` 启动，`root=PARTUUID=614e0000-0000` 与 v3 bootargs 一致。

## 2. 容量冲突

v3 FIT `boot.img` 为 37520896 字节（约 35.8 MiB），超过现有 boot/recovery 分区两倍。不能通过 `dd`、
升级工具或其他直接写盘方式把 v3 FIT 放入 p3/p4，否则可能截断镜像并破坏相邻分区。

用户已明确授权首次刷写，但容量门禁优先级更高，刷写动作未执行；旧 4.19 系统和 v3 归档均保留。

## 3. 下一步

只在 Ubuntu 主机评估压缩内核 `Image.lz4`/zboot 是否小于 16 MiB，且确认当前 U-Boot/boot 格式支持。若仍
超限，必须取得匹配的更大分区表或完整升级布局，不能自行改 ptable。

在容量路线通过前不刷写、不重启、不改板端配置、不启动网关。
