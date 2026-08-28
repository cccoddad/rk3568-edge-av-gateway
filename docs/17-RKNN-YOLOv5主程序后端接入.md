# RKNN YOLOv5 主程序后端接入

## 1. 本阶段结论

独立 YOLOv5s RKNN 工具已经完成固定图片、真实摄像头单帧和连续 10 次推理验证。本阶段把
同一套模型契约和 C17 后处理迁入主程序，新增可选 `RknnInferenceEngine`，默认 Mock 构建不
依赖厂商 SDK。

后续已经完成主程序 Mock RGB + RKNN 板端 10 秒验证，并接入 MJPEG 到 RGB888 解码层。
真实 UGREEN 摄像头协商格式为 MJPEG；解码代码已通过其 1280x720 样本回归，但实时摄像头
与 RKNN 整链路仍须在板端验收后才能表述为完成。

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
- 可选 `JpegVideoDecoder` 使用固定版本 libjpeg-turbo，在抽帧后把 CPU MJPEG 转为 RGB888，
  保持来源 sequence、PTS 和 JPEG 头中的实际尺寸。

## 4. 构建

在 Ubuntu 交叉编译机中执行：

```sh
export RKNN_SDK_ROOT="$HOME/rk3568-work/vendor/rknpu2-v1.4.0/runtime/RK356X/Linux/librknn_api"
sh ./tools/build_rknn_gateway_container.sh
```

容器构建入口固定使用 Ubuntu 22.04 的 AArch64 工具链，再调用裸机构建脚本。不能直接使用
Ubuntu 24.04 的 AArch64 C++ 运行库，因为它会引入板端不存在的 `GLIBC_2.36` 和
`GLIBC_2.38`。脚本同时启用 Mock、V4L2、ALSA 和 RKNN，静态携带
`libstdc++/libgcc`，但动态使用板端 glibc 和 `/usr/lib/librknnrt.so`。构建结束会拒绝高于
GLIBC 2.35 的符号需求，并生成
`out/aarch64-rknn-gateway/SHA256SUMS`。

`config/rk3568-rknn-rgb.json` 使用 Mock RGB 验证主程序 RKNN 生命周期；
`config/rk3568-rknn-mjpeg.json` 使用 `/dev/video9` 的 1280x720 MJPEG。两者模型路径都指向
已经持久化的 `/userdata/rkav/yolov5-runtime-712d661/yolov5s-rk3568.rknn`。

## 5. 验证状态

- Windows Debug/Release：43 项测试通过，另显式通过真实 1280x720 摄像头 JPEG 样本测试。
- 5 FPS Application 抽帧策略：集成测试通过。
- 精确 RKNN 1.4.0 头文件：严格警告语法编译通过。
- C17 YOLOv5 后处理：主机测试通过。
- AArch64 RKNN 主程序动态构建：CI 使用 GCC/G++ 11.4.0 构建通过，最高要求 GLIBC 2.34。
- 板端 Mock RGB + RKNN 10 秒运行：通过；51/51 次推理完成，NPU 中断增加 51，无新增相关内核错误。
- 真实 MJPEG 摄像头 + RKNN：解码代码已完成，等待新产物交叉构建和板端实时验证。

## 6. 下一步

1. 在 Ubuntu 交叉构建包含静态 libjpeg-turbo 的新主程序，并复核 GLIBC 2.35 上限。
2. 通过 TF 卡部署新主程序和 `rk3568-rknn-mjpeg.json`。
3. 完成真实摄像头 + Mock 音频 + RKNN 的 10 秒和 60 秒验证。
4. 再替换为真实 ALSA 麦克风，执行 30 分钟长稳验证。
