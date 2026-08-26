# RKNN 1.4.0 独立冒烟测试

更新日期：2026-08-26

## 1. 目标和边界

本阶段只验证板端现有 RKNN Runtime、驱动和 MobileNet 分类模型能够形成最小推理闭环，不修改
主音视频管道，也不把分类结果宣称为项目最终的目标检测结果。

已固定的开发文件来自 Rockchip `airockchip/rknpu2` 的 `v1.4.0`：

- 提交：`ef74cafa8013ffafe6d932f38ce2ea26d37a0283`
- 平台：`RK356X/Linux/aarch64`
- `rknn_api.h` SHA-256：`2bb8008935f6ea3df421fcc3d294887c804fff5c08e7f29c330b557bd31d65c2`
- `librknnrt.so` SHA-256：`0ebc1b408f897863a91a1b9ed60f3838a801386c7b1ef7c54d55ead624cd8347`

板端厂商 Runtime 虽然同为 1.4.0，但哈希可能不同。开发库只用于交叉链接，不能覆盖板端
`/usr/lib/librknnrt.so`。

## 2. 构建模型

主网关为了兼容旧 Buildroot 使用全静态链接；RKNN Runtime 是动态库，因此冒烟程序使用独立
CMake 子工程：

- 可执行文件动态依赖板端 `librknnrt.so` 和 glibc。
- `libstdc++` 与 `libgcc` 静态并入，降低宿主交叉工具链与旧板端的版本差异。
- 不设置构建机 RPATH，板端从 `/usr/lib` 解析 Runtime。
- SDK 文件保存在仓库外，不把厂商二进制提交到 Git。

## 3. 冒烟程序行为

`rknn-smoke` 依次执行：

1. 读取 `.rknn` 模型并调用 `rknn_init`。
2. 查询 Runtime、驱动、输入输出数量和全部 tensor 属性。
3. 按模型输入属性创建全零测试输入。
4. 执行指定次数的 `rknn_run` 和 `rknn_outputs_get`。
5. 打印首末次输出的范围、均值和 FNV-1a 摘要。
6. 释放输出和 RKNN context，成功时返回 0。

全零输入只验证执行闭环，不验证分类准确率。固定图片正确性在下一阶段单独完成。

## 4. 验收标准

- 产物是 Linux AArch64 ELF，并且 `NEEDED` 包含 `librknnrt.so`。
- 板端打印 Runtime 1.4.0 和驱动 0.8.2。
- MobileNet 的输入输出属性可读取。
- 单次和 100 次运行均返回 0。
- 100 次运行没有 NPU 驱动错误、初始化失败或持续内存增长。
- 主项目 Windows 34 项回归测试保持通过。
