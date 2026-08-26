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
- 冒烟程序使用 C17，避免静态 `libstdc++` 从 Ubuntu 24.04 引入板端不存在的新 glibc 符号。
- 不设置构建机 RPATH，板端从 `/usr/lib` 解析 Runtime。
- 构建脚本检查全部 `GLIBC_*` 符号，最高版本不得超过板端的 `GLIBC_2.35`。
- SDK 文件保存在仓库外，不把厂商二进制提交到 Git。

## 3. 板端兼容性基线

2026-08-26 在目标板读取到：

- Buildroot `2018.02-rc3-dirty`，Linux `4.19.232`，AArch64。
- glibc `2.35`，由 GCC `10.3.0` 编译；板端没有原生 GCC/G++。
- `/usr/lib/librknnrt.so` 的 MD5 为 `757e7f089e657a7fc28c67c71738f0cc`，它自身最高只要求
  `GLIBC_2.17`。
- NPU 通过 `/dev/dri/card1` 和 `/dev/dri/renderD129` 提供设备节点，没有 `/dev/rknpu`。

首次使用 Ubuntu 24.04 的 G++ 13 交叉编译时，虽然 `libstdc++` 已静态链接，产物仍最高要求
`GLIBC_2.38`。该产物不能部署到 glibc 2.35 的板端。改用 C17 后由构建脚本直接阻止这类 ABI
不兼容产物进入上板步骤。

## 4. 冒烟程序行为

`rknn-smoke` 依次执行：

1. 读取 `.rknn` 模型并调用 `rknn_init`。
2. 查询 Runtime、驱动、输入输出数量和全部 tensor 属性。
3. 未提供输入文件时按模型属性创建全零输入；提供输入文件时读取尺寸完全匹配的 UINT8 NHWC
   RGB 数据，由 RKNN Runtime 完成归一化和 INT8 量化。
4. 执行指定次数的 `rknn_run` 和 `rknn_outputs_get`。
5. 打印输入摘要，以及首末次输出的范围、均值、FNV-1a 摘要和 Top-5 类别索引。
6. 释放输出和 RKNN context，成功时返回 0。

全零输入只验证执行闭环，不验证分类准确率。固定图片正确性在下一阶段单独完成。

## 5. 验收标准

- 产物是 Linux AArch64 ELF，并且 `NEEDED` 包含 `librknnrt.so`。
- 产物要求的最高 glibc 符号版本不超过 `GLIBC_2.35`。
- 板端打印 Runtime 1.4.0 和驱动 0.8.2。
- MobileNet 的输入输出属性可读取。
- 单次和 100 次运行均返回 0。
- 100 次运行没有 NPU 驱动错误、初始化失败或持续内存增长。
- 主项目 Windows 34 项回归测试保持通过。

## 6. 板端实测结果

2026-08-26 在目标 RK3568 Buildroot 板上完成单次和 100 次连续推理：

- 模型：`/usr/share/model/RK356X/mobilenet_v1.rknn`，大小 `4419318` 字节，MD5 为
  `1f051a1d9be7411a29e22fcaff030b62`。
- Runtime API 为 `1.4.0`，NPU 驱动为 `0.8.2`。
- 输入 tensor 为 `1x224x224x3`、NHWC、INT8；输出 tensor 为 `1x1001`、INT8。
- 100 次运行退出码为 0，并打印 `smoke_test status=passed iterations=100`。
- 第 1 次和第 100 次输出的 FNV-1a 摘要均为 `0x7d870e4001f19d91`，输出稳定。
- NPU/IOMMU 中断计数从 1 增加到 101，证明 100 次请求实际提交到 NPU。
- 最后一次核心推理耗时为 `7112 us`；包含模型加载、初始化和退出的整段命令耗时约 2 秒。
- 可用内存由 `1932716 kB` 变为 `1931984 kB`，变化 732 kB；没有 Swap，也没有新增 NPU、
  IOMMU、fault、timeout 或 panic 内核日志。

该耗时只代表已经加载后的 MobileNet 全零输入核心推理。它不包含 MJPEG 解码、图像缩放、
颜色转换、真实图片读入、分类后处理、摄像头采集或画面叠加，不能直接换算成完整业务管道帧率。
至此独立 RKNN Runtime/NPU 冒烟测试通过，下一阶段是用固定真实图片验证预处理和分类结果。

固定 RGB 输入的调用形式为：

```sh
LD_LIBRARY_PATH=/usr/lib rknn-smoke model.rknn 1 input-224x224-rgb.bin
```

输入文件必须是紧密排列的 RGB888；对于 `224x224x3` 模型，文件大小必须严格等于 150528
字节。冒烟程序不隐式猜测图片格式，也不会把 JPEG 压缩数据直接作为 tensor。

## 7. 摄像头单帧预处理实测

2026-08-26 在板端直接从绿联摄像头 `/dev/video9` 重新抓取当前画面：

- 采集格式为 `1280x720 MJPEG`、30 FPS，单帧保存为
  `/userdata/rkav/input/camera-1280x720.jpg`。
- JPEG 文件约 60 KiB，MD5 为 `c594b6ee0911858b29d40164b94af4d3`，GStreamer 正确识别为
  `image/jpeg, width=1280, height=720`。
- 使用板端 GStreamer 完成 JPEG 解码、缩放和 RGB 转换；运行时打印 RGA API 1.3.2 信息。
- 生成 `/userdata/rkav/input/camera-224x224-rgb.bin`，大小严格等于 150528 字节，SHA-256
  为 `63135b732665225f0bcef32b472cd15e5548d95681fcd32e4e06d36b1f442bd0`。

这一步证明真实摄像头 MJPEG 可以转换为当前 MobileNet 的输入尺寸和布局。

## 8. 真实摄像头单帧 NPU 推理实测

部署支持 RGB 文件参数的新版 `rknn-smoke` 后，使用上一节生成的真实摄像头输入完成推理：

- 上板程序 SHA-256 为 `66b27f84b98142a35befae410859dca6861ec856e8c8aa9dc5f2dedfc4d89d89`。
- 输入以 `UINT8`、NHWC 提交，150528 字节的 FNV-1a 摘要为 `0x8d8fb29a6e85ed07`。
- 输出包含 1001 个有限浮点值，FNV-1a 摘要为 `0xa558f10d24493a87`。
- Top-5 类别索引依次为 572、779、618、838、439；最高分为 `0.10546875`。
- 核心推理耗时为 `6224 us`，程序退出码为 0，并打印
  `smoke_test status=passed iterations=1`。
- NPU/IOMMU 中断计数从 101 增加到 102，期间没有新增 NPU、IOMMU、fault、timeout 或 panic
  内核日志。

至此已经证明真实摄像头数据可以完成抓取、JPEG 解码、缩放、RGB 转换和 RKNN/NPU 推理。
当前模型仍是整图分类模型，最高分较低且板端没有类别表；这些索引不作为可靠业务识别结果。
项目下一阶段按既定方案选择与 Runtime 1.4.0 兼容的目标检测模型，输出检测框、类别和置信度。
