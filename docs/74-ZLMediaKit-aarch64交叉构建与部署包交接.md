# ZLMediaKit aarch64 交叉构建与部署包交接

更新日期：2026-09-13
状态：**阶段 2 部署包已就绪**（构建与静态验证完成；板端运行为最终验收，待硬件）

## 1. 当前结论

ZLMediaKit 已按 **与 PC 验证完全相同的 commit `4b07053`** 交叉编译为 aarch64 部署包：

1. **工具链与门禁**：Ubuntu 22.04 容器内 `aarch64-linux-gnu-gcc 11.4.0`，sysroot GLIBC 2.35；
   产物实测最高只要求 **GLIBC_2.34**，低于板端 2.35 上限，门禁通过；
2. **依赖自包含**：OpenSSL 3.0.15 与 libsrtp 2.6.0 交叉静态/动态构建并打包；MediaServer
   仅动态依赖 `libssl.so.3`、`libcrypto.so.3`、`libstdc++.so.6`、`libgcc_s.so.1`、glibc，
   其中前四个随包提供（`lib/` + `start.sh` 设置 `LD_LIBRARY_PATH`）；
3. **功能对齐 PC 验证版本**：`ENABLE_WEBRTC=ON`（WebRTC 插件与 `www/webassist` 播放页已包含），
   HLS/MP4/RTPPROXY 开启，SRT/SCTP/MySQL/FFmpeg 关闭，`DISABLE_REPORT=ON`；
4. **部署材料齐备**：`start.sh`、systemd 单元 `rkav-zlmediakit.service`（板端 Debian/GEC 路线）、
   SysV 启停脚本 `rkav-zlmediakit.init`（当前 Buildroot 镜像）、`conf/config.ini`、`BUILD_INFO.txt`。

## 2. 部署包

| 项目 | 值 |
|---|---|
| 包名 | `rkav-zlmediakit-aarch64-20260913-232946.tar.gz`（14.4 MB） |
| SHA-256 | `7024938012d1c362f9e1c1610444a6b20c541eef960c616a743443396cc1a11c` |
| 暂存位置 | WinDownloads（`C:\Users\CC\Downloads\`）与证据目录 `zlm-aarch64-build-20260913-205447` |
| 内容 | `bin/MediaServer`、`lib/`（含 libssl/libcrypto/libstdc++/libgcc_s/libmk_api）、`www/`（含 webassist 与 swagger）、`conf/config.ini`、`start.sh`、systemd/SysV 启动脚本、`BUILD_INFO.txt`、`SHA256SUMS` |
| ZLM 版本 | commit `4b07053aa5f6d35505e8c548c2a08a978143fcf1`（与 Docker 镜像 `4b07053` 一致） |
| 依赖版本 | OpenSSL 3.0.15、libsrtp 2.6.0 |
| 构建入口 | 工具链文件 `aarch64.cmake`；容器构建脚本与日志见证据目录 |

## 3. 构建要点

- **源码获取**：Gitee 镜像 `git clone --depth 1 --recurse-submodules`（ZLToolKit、media-server、
  jsoncpp、pybind11、webassist 子模块齐全；GitHub tarball 不含子模块内容）。
- **依赖交叉编译**：OpenSSL `./Configure linux-aarch64 --cross-compile-prefix=aarch64-linux-gnu-`；
  libsrtp `./configure --host=aarch64-linux-gnu`；安装到持久化前缀 `/opt/zlm-deps`
  （bind mount 到宿主 `$WORK/prefix`，重试时跳过已构建依赖）。
- **ZLM CMake 关键项**：`-DENABLE_PLAYER=ON`（**必须**：  `MediaServer` 的 `PlayerProxy`
  依赖 `MediaPlayer`，设为 OFF 会在链接期报 vtable 未定义）、`-DENABLE_TESTS=OFF`、
  `-DENABLE_OBJCOPY=OFF`（交叉环境无宿主 objcopy）、`-DSRTP_PREFIX`、`-DOPENSSL_ROOT_DIR`。

## 4. 过程问题记录

1. **虚拟机中途关机导致对象文件截断**：首次构建进行到 55% 时宿主关机，部分 `.o` 为 0 字节
   （甚至符号不全的非 0 文件），`make` 依据时间戳误判为最新，续跑时链接大量未定义符号。
   处置：清空 `ZLMediaKit/build` 与 `release/linux` 后全量重编（依赖前缀保留），一次通过。
   教训：长构建任务被强制中断后，不要只删 0 字节对象，应清理整棵构建树。
2. **CMake 缓存**：首轮 `-DENABLE_PLAYER=OFF` 被写入 `CMakeCache.txt`，后续省略该参数不会
   恢复默认值，必须显式 `-DENABLE_PLAYER=ON` 覆盖。
3. **SRT 关闭**：`ENABLE_SRT=OFF`（SRT 需额外依赖），WebRTC 保留；后续如需 SRT 再单独开。

## 5. 板端部署步骤（阶段 2，需硬件与另行确认）

1. 解包到 `/opt/rkav/zlm`，`chmod +x start.sh bin/MediaServer rkav-zlmediakit.init`；
2. 按端口策略修改 `conf/config.ini`：特权端口 554/80 需 root，建议 RTSP 改 8554、HTTP 改 8080；
3. systemd 路线（GEC/5.10 Debian）：安装 `rkav-zlmediakit.service`；
   当前 Buildroot：把 `rkav-zlmediakit.init` 放到 `/etc/init.d/S99zlmediakit`；
4. 网关推流地址指向 `rtsp://<板端IP>:8554/live/camera`（网关配置零改动，仅改路径）；
5. 验收：本机 `curl` ZLM API 确认流注册、多客户端并发拉流、WebRTC 浏览器播放、
   与网关断连恢复联动；记录板端 CPU/内存占用（PC 参考：CPU 5.6%、内存 8.5 MB）。

## 6. 边界

- 本次未在 aarch64 实机运行（无 qemu 环境），板端启动与运行为最终验收；
- 部署包使用动态链接 + `LD_LIBRARY_PATH` 方式，若板端 glibc 2.35 与 sysroot 存在细节差异，
  以板端实测为准；
- WebRTC 在容器/PC 上仅验证服务端就绪；板端浏览器播放待阶段 2；
- 摄像头/麦克风/NPU 仍由网关独占，ZLM 只占网络与少量 CPU。

## 7. 涉及名词

- **GLIBC 符号版本门禁**：用 `objdump -T` 查看二进制引用的最高 `GLIBC_x.y`，必须不高于
  板端 glibc 版本；本次上限 2.34（板端 2.35）。
- **静态与动态依赖**：libsrtp 静态进库，OpenSSL 动态随包；`LD_LIBRARY_PATH` 优先加载包内库。
- **持久化依赖前缀**：把容器内 `/opt/zlm-deps` 绑定到宿主目录，构建重试时复用，避免重复编译。

## 8. 2026-09-14 板端部署记录（阶段 2 部署执行）

- **部署位置**：`/opt/rkav/zlm`（`/userdata/rkav` 保留包副本），板端 glibc 2.35 运行正常；
- **端口**：`conf/config.ini` 改为 `[rtsp] port=8554`、`[http] port=8080`（其余保留默认）；
- **启动方式**：`LD_LIBRARY_PATH=$PWD/lib ./bin/MediaServer -d -c conf/config.ini -l 1`（守护模式）；
  未纳入 init/systemd 托管，重启后需手工拉起（见 P124）；
- **API secret**：首次启动自动把默认 secret 改为 `y4YVy5XjFCAffNR2h14glk20XxexRw8u`
  （与阶段 1 Docker 行为一致，见 P120），管理接口需用新值；
- **实测**：网关以 `rtsp://127.0.0.1:8554/live/camera` 推流（MPP H.264 + FFmpeg AAC），ZLM 记录
  `originTypeStr=rtsp_push`，VM 从 `rtsp://192.168.50.2:8554/live/camera` 拉流成功；
  杀/重启 ZLM 后网关约 1.5 秒自动重连。完整证据见
  [板端 RTSP 推流与断连恢复及 FFmpeg 交叉构建交接](77-板端RTSP推流与断连恢复及FFmpeg交叉构建交接.md)；
- **待做验收**：板端多客户端并发、FLV/RTMP/HLS 实测、WebRTC 浏览器播放、端到端延迟测量与资源占用。

## 9. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [ZLMediaKit 流媒体服务层升级方案](72-ZLMediaKit流媒体服务层升级方案.md)
- [ZLMediaKit 阶段 1 PC 验证交接](73-ZLMediaKit阶段1PC验证交接.md)
