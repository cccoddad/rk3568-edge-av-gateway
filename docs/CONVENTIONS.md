# 项目开发约定

本文件定义 RK3568 实时音视频边缘分析网关的编码、验证与协作约定，适用于所有贡献者。

## 代码风格

- 格式化：Google 风格基线 + 4 空格缩进 + 100 列限制，以 `.clang-format` 为准；
- 命名：命名空间 `lower_case`，类与函数 `CamelCase`，变量 `lower_case`，私有成员以 `_` 结尾；
- 告警：`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor -Werror`；
- 静态检查：`.clang-tidy` 覆盖 bugprone / concurrency / modernize / performance；
- 注释：每个 `.cpp` 首行注明文件作用与主要知识点；函数用 `/// 功能：`、`/// 返回：`。

## 架构边界

- `rkav_core` 只放平台无关的类型、算法与配置，**不依赖** V4L2 / RKNN / MPP 等硬件 SDK；
- Rockchip SDK 类型不得出现在 `rkav_core` 公共头文件中；
- Mock 后端独立成库，真实后端可并存——新增后端不修改公共数据契约与 Application 主流程；
- 每个跨线程队列必须声明固定容量与溢出策略（`drop_oldest` / `keep_latest` / `block_producer`）。

## 配置与日志

- 配置为严格字段校验：拼写错误、类型错误、范围越界、选择未编译后端，均在线程启动前失败；
- 日志每行一个 JSON 对象，按 `module` / `event` / `level` 检索；
- 禁止在每帧路径打印 INFO 日志，日志 I/O 不得干扰实时链路。

## 测试与验收纪律

- 每个工作单元必须满足：新增/修改行为有对应测试，`ctest` 全绿后才算完成；
- 验收遵循"证据可复核"：唯一结果目录、运行前后 `dmesg` 快照、关键文件 SHA-256 清单；
- 长稳类验收先定判据（退出码、错误计数、队列丢弃、内核日志差异）再开跑，
  事后按判据逐条对账，不允许跑完再补标准；
- 失败时先保存日志与证据再分析，不盲目重跑；板端现场操作遵循"先取证、后重启"。

## 板端硬件安全

- 摄像头、麦克风与 NPU 由本网关独占，同一时刻只允许一个网关进程；
- 板卡重启 = 拔全部线（含电源线）15 秒后重新上电；**本板永久禁止插入 TF 卡、
  禁止刷写 p1/p3 分区**（白屏变砖事故教训，见问题台账 P127）；
- 冷启动后 RTC 不可用，任何板端作业前先 `date -u -s` 对时；
- 重启/拔插/改分辨率/FPS/端口类操作需先确认无长稳任务在跑。

## Git 约定

- 提交粒度：一个完整工作单元一个提交，提交信息使用 `fix:` / `feat:` / `docs:` / `test:` 前缀；
- 禁止 `git reset --hard`、`git checkout --`、`git clean`、force push；
- 提交排除：密钥、Token、模型文件、SDK 二进制、数据库、媒体原件、板端原始日志、构建目录；
- 推送使用仓库约定的网络代理（见文档《Git保存与GitHub上传指南》）；
- 每个工作单元完成后更新 `docs/19-项目当前开发状态.md` 与最新交接文档。

## 构建与环境

- Windows：目录名含特殊字符，须经 `tools/build_windows.ps1`（短路径 Junction）构建；
- Linux：`tools/build_and_test.sh`；交叉编译见 `tools/build_rknn_gateway.sh`；
- 工具链门禁：板端可执行文件 GLIBC 版本必须 ≤ 板上运行环境；
- Sanitizer：`asan` 预设（ASan+UBSan）作为内存类改动的必跑项。
