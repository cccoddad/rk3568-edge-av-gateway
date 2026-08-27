# RK3568 YOLOv5s RKNN 目标检测验证

## 1. 阶段边界

本阶段把独立 RKNN 验证从 MobileNet 分类推进到 YOLOv5s 目标检测模型，已经完成：

- 使用 RKNN Toolkit2 1.4.0 将官方 YOLOv5s ONNX 转换为 RK3568 INT8 RKNN 模型；
- 在 x86_64 转换机模拟器上得到可重复的类别、置信度和检测框；
- 在 RK3568 板端真实 NPU 上加载模型并取得三路完整输出张量；
- 在仓库中实现与官方示例公式一致的纯 C17 解码、阈值过滤和逐类别 NMS。

这一阶段仍是独立工具验证，主音视频管道的 inference 配置仍为 Mock。只有后续把 RKNN
后端接入 `IInferenceEngine` 并完成实时摄像头验收后，才能把主程序描述为真实 NPU 检测。

## 2. 固定版本与输入

| 项目 | 固定值 |
| --- | --- |
| 转换工具 | `rknn-toolkit2==1.4.0-22dcfef4` |
| 转换环境 | Ubuntu 20.04、Python 3.8.10、x86_64 Docker 镜像 |
| 目标平台 | `rk3568`，转换脚本显式指定 |
| 原始模型 | 官方 v1.4.0 示例 `yolov5s.onnx` |
| 量化数据 | 官方示例 `bus.jpg` |
| 模型输入 | `1x640x640x3`、NHWC、INT8，Runtime 输入使用 RGB888 UINT8 |
| 模型输出 | `1x255x80x80`、`1x255x40x40`、`1x255x20x20` |
| RKNN 模型 SHA-256 | `dccc47b4988107957a1806f6a85b688ee36b86074c7ba21231782b6ea8bdac0d` |
| RGB 输入 SHA-256 | `a6d5d92be093dbf380a35bee459ebc631b44d5ee863f956a380486e7f2aed5a3` |

转换时曾出现 `target_platform is None, use rk3566 as default`。该产物只用于验证转换流程，
没有部署。最终转换脚本显式设置 `target_platform='rk3568'`，并重新完成量化、导出和模拟器
推理。模拟器的 `Target is None, use simulator` 只表示没有连接开发板，不代表模型平台未指定。

## 3. x86_64 模拟器参考结果

官方 `bus.jpg` 在 Toolkit2 模拟器中得到 5 个 NMS 后结果：

| 类别 | 置信度 | 框坐标 `[left, top, right, bottom]` |
| --- | ---: | --- |
| person | 0.8223356 | `[473.267, 231.938, 562.127, 519.760]` |
| person | 0.8179780 | `[211.990, 245.029, 283.708, 513.937]` |
| person | 0.7971193 | `[115.250, 232.442, 207.784, 546.110]` |
| person | 0.4627231 | `[79.092, 339.180, 121.600, 514.235]` |
| bus | 0.7545359 | `[86.417, 134.418, 558.108, 460.418]` |

阈值沿用官方 v1.4.0 示例：object/class probability 阈值为 `0.25`，逐类别 NMS IoU 阈值为
`0.45`。

## 4. RK3568 真实 NPU 实测

目标板环境保持不变：

- Buildroot 2018.02-rc3，Linux 4.19.232，AArch64；
- glibc 2.35；
- RKNN Runtime API 1.4.0，NPU driver 0.8.2；
- `/usr/lib/librknnrt.so` MD5 为 `757e7f089e657a7fc28c67c71738f0cc`。

板端先对 TF 卡部署包执行 SHA-256 全量校验，再复制到 `/tmp`，卸载 TF 卡后运行。实测结果：

- `rknn_init`、输入设置、执行、三路输出读取和释放全部成功；
- 三路输出元素数分别为 `1632000`、`408000` 和 `102000`；
- 三路反量化浮点输出全部为有限值；
- 核心推理耗时 `95564 us`；
- NPU 中断计数从 `0` 增加到 `1`；
- 推理退出码为 0，内核日志没有 NPU 或 IOMMU fault。

这证明最终 RK3568 模型不是只在转换机模拟器可用，而是已经在目标板真实 NPU 上完整执行。

## 5. C17 后处理实现

`tools/rknn_smoke/yolov5_postprocess.c` 不依赖 RKNN SDK，可在主机单独测试。它实现：

1. 严格验证三个输出网格与 `8/16/32` stride 的对应关系；
2. 按 NCHW 布局读取三组 anchor 的 `x/y/w/h/object/class` logits；
3. 使用 YOLOv5 的 sigmoid、`xy*2-0.5` 和 `(wh*2)^2*anchor` 公式解码；
4. 选择每个候选框的最高类别概率；
5. 按置信度稳定排序并执行逐类别 NMS；
6. 限制返回容量并显式报告截断，避免无界输出。

`tools/test_yolov5_postprocess.sh` 使用严格编译警告构建合成张量测试，覆盖三尺度候选解码、
同类别重叠框抑制、不同类别保留、输出容量截断和非法网格拒绝。CI 先运行该测试，再使用锁定的
RKNN 1.4.0 开发文件交叉编译 AArch64 `rknn-smoke`。

## 6. 下一步

下一次板端验证使用新版 `rknn-smoke` 对同一模型和 RGB 输入执行真实 NPU 推理，并将 C17
后处理输出与第 3 节的模拟器参考框逐项比较。通过后再进入：

1. 从 `/dev/video9` 抓取实时 MJPEG 帧；
2. JPEG 解码并按模型契约转换为 640x640 RGB；
3. 调用 RKNN 并输出真实检测框；
4. 把独立工具中的后处理迁入 `RknnInferenceEngine`；
5. 完成真实摄像头、麦克风和 RKNN 的 10 秒、60 秒与长稳验收。
