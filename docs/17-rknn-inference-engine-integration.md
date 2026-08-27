# RKNN YOLOv5 主程序后端接入

## 1. 本阶段结论

独立 YOLOv5s RKNN 工具已经完成固定图片、真实摄像头单帧和连续 10 次推理验证。本阶段把
同一套模型契约和 C17 后处理迁入主程序，新增可选 `RknnInferenceEngine`，默认 Mock 构建不
依赖厂商 SDK。

当前代码完成了 RGB/BGR 到模型输入、NPU 推理和检测框回映射，但真实 UGREEN 摄像头协商
格式是 MJPEG。MJPEG 解码层尚未接入主程序，因此本阶段不能表述为“实时摄像头目标检测已经
完成”。

## 2. 板端基线

2026-08-27 的连续推理增量验证结果：

| 项目 | 结果 |
|---|---:|
| 模型 | YOLOv5s RK3568 INT8 |
| Runtime / 驱动 | 1.4.0 / 0.8.2 |
| 连续推理次数 | 10 |
| 最后一次核心 NPU 耗时 | 93445 us |
| 含输出和日志的整体吞吐 | 5.848 次/秒 |
| NPU 中断增量 | 10 |
| 新增 NPU/IOMMU 错误 | 0 |
| 温度 | 46.111 -> 48.333 摄氏度 |

开机阶段 6 条 `failed` 日志均来自设备树未提供 NPU 功耗模型参数，没有在本轮推理期间新增。
证据目录为 `/userdata/rkav/yolov5-kernel-delta/uptime-614-851`。

## 3. 实现内容

- `RknnInferenceEngine::Open` 读取模型、初始化 context，并验证一个 640x640 NHWC 输入和
  80/40/20 三路 NCHW 输出。
- `Infer` 只接受 CPU RGB888/BGR888，使用最近邻缩放到模型输入并由 Runtime 完成 UINT8 到
  INT8 量化转换。
- 输出使用 `want_float=1`，复用 `yolov5_postprocess.c` 完成三尺度解码和逐类别 NMS。
- 模型坐标按 X/Y 比例映射回传入 `VideoFrame` 的来源尺寸，结果保留来源 sequence 和 PTS。
- RKNN 输出通过 RAII 保证在所有错误路径释放，`Close` 可重复调用。
- Application 在推理队列前按 `max_fps` 抽帧；生产配置固定为 5 FPS、容量 1、
  `keep_latest`。

## 4. 构建

在 Ubuntu 交叉编译机中执行：

```sh
export RKNN_SDK_ROOT="$HOME/rk3568-work/vendor/rknpu2-v1.4.0/runtime/RK356X/Linux/librknn_api"
sh ./tools/build_rknn_gateway.sh
```

脚本同时启用 Mock、V4L2、ALSA 和 RKNN，静态携带 `libstdc++/libgcc`，但动态使用板端 glibc
和 `/usr/lib/librknnrt.so`。构建结束会拒绝高于 GLIBC 2.35 的符号需求，并生成
`out/aarch64-rknn-gateway/SHA256SUMS`。

`config/rk3568-rknn-rgb.json` 当前使用 Mock RGB 视频验证主程序 RKNN 生命周期，模型路径指向
已经持久化的 `/userdata/rkav/yolov5-runtime-712d661/yolov5s-rk3568.rknn`。

## 5. 验证状态

- Windows 默认 Mock 构建：37 项测试通过。
- 5 FPS Application 抽帧策略：集成测试通过。
- 精确 RKNN 1.4.0 头文件：严格警告语法编译通过。
- C17 YOLOv5 后处理：主机测试通过。
- AArch64 RKNN 主程序动态构建：等待 Ubuntu 执行。
- 板端 Mock RGB + RKNN 10 秒运行：等待动态构建产物。
- 真实 MJPEG 摄像头 + RKNN：等待 MJPEG 解码预处理层。

## 6. 下一步

1. 在 Ubuntu 执行 `build_rknn_gateway.sh`，确认 ELF、动态依赖和 GLIBC 上限。
2. 通过 TF 卡把主程序和配置部署到板端，先运行 Mock RGB + RKNN 10 秒。
3. 新增独立 MJPEG 解码预处理接口，把 `/dev/video9` 帧转换为 CPU RGB888。
4. 完成真实摄像头、ALSA 麦克风和 RKNN 的 10 秒、60 秒及长稳验证。
