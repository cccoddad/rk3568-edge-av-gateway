# ZLMediaKit 阶段 1 PC 验证交接

更新日期：2026-09-12
状态：**已完成**（PC 回环验证；板端部署属阶段 2，待 MPP/RTSP 板端通过后实施）

## 1. 当前结论

网关（现有 FFmpeg RTSP push 输出，**零代码改动**）推流到 ZLMediaKit 成功，验证结果：

1. **3 路客户端并发 RTSP 拉流**：`getMediaList` 显示 `readerCount=3`，三个客户端各自捕获
   10.01 秒完整 H.264+AAC，互不干扰；
2. **多协议输出**：HTTP-FLV 5.007 秒、RTMP 5.021 秒、HLS 播放列表 HTTP 200 且含有效分片，
   全部为 H.264+AAC；
3. **ZLM 重启联动**：容器重启后网关 `connection_lost=1`、`reconnected=1`，恢复推流后
   客户端再次拉流成功（5.015 秒）；
4. **ZLM 资源占用极低**：CPU 5.6%、内存 8.5 MB（空闲态快照）；
5. **WebRTC**：插件加载（push/play/talk）、UDP/TCP 8000 端口就绪、测试页 HTTP 200；
   浏览器端到端播放验证留待桌面浏览器/板端阶段。

**未测项（不写数字）**：端到端延迟（需要画面时钟参照）、UDP 传输下的多客户端、
浏览器 WebRTC 播放、多客户端长时间并发稳定性。

## 2. 环境与获取方式

| 项目 | 值 |
|---|---|
| ZLM 版本 | 官方镜像 `zlmediakit/zlmediakit:master`，git hash `4b07053`/2026-09-09，构建时间 2026-09-09 |
| 镜像来源 | `m.daocloud.io/docker.io/zlmediakit/zlmediakit:master`（Docker Hub 直连被拒），digest `sha256:6897159b...edc033` |
| 端口映射 | RTSP 8554→554、HTTP 8888→80、RTMP 1935→1935、WebRTC 8000→8000（TCP+UDP） |
| 网关 | leakfix Release 构建（含断连恢复、传输选项、超时守卫、会话收尾修复） |
| 推流地址 | `rtsp://127.0.0.1:8554/live/camera`（ZLM 路径格式 app/stream） |
| 环境 | VMware Ubuntu 24.04 虚拟机，回环网络，Mock 源 + FFmpeg 软编码 |

## 3. 验证结果明细

| 测试 | 结果 | 证据文件 |
|---|---|---|
| 推流注册 | API `code=0`，流 `live/camera`，轨道 `H264` + `mpeg4-generic` 均 ready | `medialist.json` |
| 3 路并发 RTSP | readers=3；三个客户端 10.011/10.011/10.011 秒，均 H.264+AAC | `medialist-during.json`、`client-1..3.mp4` |
| HTTP-FLV | 5.007 秒 H.264+AAC，无错误 | `flv.mp4`、`flv.log` |
| RTMP | 5.021 秒 H.264+AAC，无错误 | `rtmp.mp4`、`rtmp.log` |
| HLS | HTTP 200，`#EXTM3U`、`#EXT-X-TARGETDURATION:2`、分片序号 13 | `hls.m3u8` |
| WebRTC 就绪 | 测试页 HTTP 200；日志含 webrtc plugin 与 8000 端口绑定 | 主日志 |
| ZLM 重启恢复 | `connection_lost=1`、`reconnected=1`；恢复后拉流 5.015 秒 | `gateway.log`、`after-restart.mp4` |
| 资源占用 | CPU 5.59%、内存 8.547 MiB / 3.773 GiB | 主日志 |

证据目录：共享目录 `zlm-stage1b-20260912-224007`（Windows 侧 `D:\share\` 同名目录），
含全部日志、捕获文件和 `SHA256SUMS`。关键哈希：`gateway.log` `47e5471f...f9d0c`、
`client-1.mp4` `64dfe530...d0f67`、`flv.mp4` `c51f77c8...adc26f`。

## 4. 过程问题与修正（保留记录）

1. **首轮脚本 `wait` 作用域错误**：test 1 后的裸 `wait` 等待了后台网关进程，后续测试都在
   网关 90 秒时长结束后执行，FLV/RTMP 报 404、重启测试无效；首轮证据
   `zlm-stage1-20260912-223432` 仅 3 路并发 RTSP 部分有效，其余作废。修正为
   `wait $P1 $P2 $P3` 并延长时长后重跑。
2. **ZLM API secret 自动重置**：镜像首次启动时默认 secret 被判无效，日志打印新 secret
   （`modified it to: ...`），API 未带正确 secret 返回 `code=-100 Please login first`；
   脚本改为从容器日志解析实际 secret。
3. **getMediaList 同流多条记录**：ZLM 对同一路流返回多条条目（不同 schema/轨道组合），
   `readerCount=3` 出现在其中一条；统计时取最大值。
4. 接收端 mp4 muxer 的 `Timestamps are unset in a packet for stream 0` 为已知接收端警告，
   产物完整，不影响结论。

## 5. 边界与阶段 2 待办

- 本验证为 PC 回环 + Mock 源 + FFmpeg 软编码，不代表板端 MPP 编码与真实网络；
- 浏览器 WebRTC、延迟测量、UDP 多客户端、多客户端长稳（1 小时+）尚未进行，可列为阶段 2 补充；
- 板端部署需 aarch64 方案（官方镜像不可用于 Buildroot；阶段 2 再评估预编译/交叉编译/
  systemd 托管），排在板端 MPP/RGA 与 RTSP 验证之后；
- 简历口径：只写实测数字，如"网关推流至 ZLMediaKit，3 路客户端并发 RTSP 拉流与
  HTTP-FLV/RTMP/HLS 播放验证通过，服务端占用 CPU 5.6%/内存 8.5 MB（PC 平台）"。

## 6. 涉及名词

- **getMediaList / readerCount**：ZLM HTTP API，前者列出当前流与轨道，后者显示正在拉流的
  客户端数量，用于证明并发。
- **HTTP-FLV / RTMP / HLS**：三种常见分发协议；FLV 适合浏览器低延迟，HLS 适合兼容性分发，
  RTMP 常用于向直播平台转推。
- **容器端口映射**：ZLM 容器内以特权端口 554/80 监听，映射到宿主 8554/8888，避免与系统
  服务冲突。

## 7. 相关文档

- [项目当前开发状态](19-项目当前开发状态.md)
- [ZLMediaKit 流媒体服务层升级方案](72-ZLMediaKit流媒体服务层升级方案.md)
- [RTSP 断连恢复实现与 PC 端到端验证交接](69-RTSP断连恢复实现与PC端到端验证交接.md)
- [RTSP 传输选项与超时守卫实现及 PC 验证交接](70-RTSP传输选项与超时守卫实现及PC验证交接.md)
