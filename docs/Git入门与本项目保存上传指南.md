# Git 入门与本项目保存、上传指南

更新日期：2026-08-21

## 1. 文档用途

这既是 Git 新手手册，也是本项目第一次保存到 Git 并上传 GitHub 的操作记录。项目远端为：

```text
https://github.com/cccoddad/rk3568-edge-av-gateway
```

本项目使用 `main` 主分支。源码、测试、配置、脚本和文档纳入 Git；能重新生成的 `build/`、
`out/`、日志和分析产物由 `.gitignore` 排除，不上传 GitHub。

## 2. Git 的四个位置

```text
工作区             暂存区              本地仓库               GitHub 远端
正在编辑的文件  -> 下次提交的候选内容 -> 一次次不可变的提交记录 -> 联网备份与协作仓库
                  git add             git commit             git push
```

- **工作区**：当前项目目录中能看到和编辑的文件。
- **暂存区**：用 `git add` 选中的、准备进入下一次提交的内容。
- **本地仓库**：隐藏目录 `.git`，保存提交历史、分支和远端配置。
- **远端仓库**：GitHub 上的仓库。本地提交不会自动上传，仍需 `git push`。

编辑器保存、`git commit` 和 `git push` 是三件事：前者只保存当前文件，`commit` 保存到本地
历史，`push` 才上传 GitHub。

## 3. 本项目首次保存与上传的实际操作

### 3.1 上传前检查

```powershell
# 查看当前分支、修改和未跟踪文件
git status --short --branch

# 确认 build 确实被忽略
git check-ignore -v build

# 查看远端是否已有提交；空输出表示远端没有分支和提交
git ls-remote https://github.com/cccoddad/rk3568-edge-av-gateway.git
```

本次还检查了大文件和常见敏感信息模式，没有发现私钥、访问令牌、密码文件或需要 Git LFS
管理的大文件。自动扫描不能代替人工审查；以后提交 `.env`、配置、日志和板端证据前，仍要
确认其中没有密码、Token、Wi-Fi 密码、私人服务器信息或个人信息。

### 3.2 只为本仓库设置提交身份

```powershell
git config user.name "cccoddad"
git config user.email "202125436+cccoddad@users.noreply.github.com"

git config --get user.name
git config --get user.email
```

这里没有写 `--global`，因此只影响本项目，不改变电脑上其他仓库。邮箱使用 GitHub 隐私邮箱，
避免公开私人邮箱。提交身份只决定作者信息，不等于 GitHub 登录凭据。

### 3.3 连接远端仓库

```powershell
git remote add origin https://github.com/cccoddad/rk3568-edge-av-gateway.git
git remote -v
```

`origin` 是远端的惯用简称。`remote add` 只记录地址，不上传文件。若 `origin` 已存在但地址
错误，使用：

```powershell
git remote set-url origin https://github.com/cccoddad/rk3568-edge-av-gateway.git
```

### 3.4 选择并复查文件

```powershell
# 暂存当前目录下所有未被 .gitignore 排除的变化
git add .

# 查看暂存结果和具体差异
git status
git diff --cached
git diff --cached --check
```

`git add .` 既不是提交也不是上传，只是选择下次提交的内容。误加文件时可取消暂存，同时保留
工作区内容：

```powershell
git restore --staged <文件路径>
```

### 3.5 创建本地提交

```powershell
git commit -m "chore: establish RK3568 mock gateway baseline"
```

提交是一份带作者、时间和说明的项目快照。说明应描述完成了什么，不要长期使用 `update`、
`123` 之类无法检索的文字。提交后检查：

```powershell
git log --oneline --decorate -5
git show --stat --oneline HEAD
```

`HEAD` 表示当前分支最新提交，完整提交号可用 `git rev-parse HEAD` 查看。

### 3.6 第一次上传 GitHub

```powershell
git push -u origin main
```

- `push`：把本地提交发送到远端。
- `origin`：目标远端简称。
- `main`：目标分支。
- `-u`：建立本地 `main` 与 `origin/main` 的跟踪关系。只需设置一次，以后通常直接运行
  `git push` 和 `git pull`。

Windows Git 通常通过 Git Credential Manager 打开浏览器完成 GitHub 登录。GitHub 不接受
账户密码作为 Git HTTPS 密码；不要把 Personal Access Token 写入远端 URL、脚本、文档或
配置文件。

### 3.7 上传后验证

```powershell
git status --short --branch
git ls-remote --heads origin main
git rev-parse main
git rev-parse origin/main
```

两个 `rev-parse` 输出相同，且 `git status` 显示与 `origin/main` 同步，才表示上传完整成功。
还应打开 GitHub 仓库检查 README、目录和最新提交。

## 4. 本次操作记录

| 项目 | 结果 |
|---|---|
| 本地分支 | `main` |
| 远端简称 | `origin` |
| GitHub 地址 | `https://github.com/cccoddad/rk3568-edge-av-gateway.git` |
| 提交作者 | `cccoddad` |
| 提交邮箱 | `202125436+cccoddad@users.noreply.github.com` |
| 敏感信息检查 | 未发现私钥、Token、密码文件或疑似凭据 |
| 大文件检查 | 未发现 5 MB 以上的待提交文件 |
| 上传前验证 | Windows 构建成功，配置校验成功，30/30 项测试通过 |
| 首次提交 | `05db5309da39a422495df833225e2a15a23b9f50` |
| 首次推送 | `main -> origin/main`，已成功建立跟踪关系 |
| 首次远端验证 | 本地 `main`、`origin/main` 和 GitHub `refs/heads/main` 均指向 `05db5309...` |

提交号由提交内容计算，提交前无法预先填写。本表已在首次上传成功后填入实测结果。任何时候
都可重新取得当前实际记录：

```powershell
git log --oneline --decorate --all
git remote -v
git status --short --branch
```

## 5. `.gitignore` 的作用

`.gitignore` 告诉 Git 哪些**未跟踪文件**不应纳入版本控制。本项目忽略：

- `/build/`：CMake/Ninja 构建产物。
- `/out/`：运行、长稳和板端验收产物，通常较大且可能含环境信息。
- `/.cache/`：本地缓存。
- `/.vscode/settings.json`：个人编辑器设置。
- `compile_commands.json`：生成的编译数据库。
- `*.log`、`*.core`、`*.profraw`、`*.profdata`：日志、崩溃和分析产物。
- `.DS_Store`、`Thumbs.db`：操作系统生成文件。

忽略不等于删除，文件仍留在电脑上。`.gitignore` 不会自动作用于已经跟踪的文件。若确实要
停止跟踪但保留本地文件，先确认影响，再使用 `git rm --cached <文件路径>`。

不要提交整个 SDK 或工具链。Rockchip SDK、交叉工具链和大型模型通常应通过官方来源、校验值
和单独依赖说明管理；是否使用 Git LFS 需要另行决定。

## 6. 日常保存流程

```powershell
# 1. 开工前同步
git pull --ff-only

# 2. 修改后查看工作区
git status
git diff

# 3. 构建和测试；本项目 Windows 基线命令
powershell -ExecutionPolicy Bypass -File .\tools\build_windows.ps1

# 4. 选择本次内容；确认全部变化属于同一任务时可用 git add .
git add <文件1> <文件2>

# 5. 复查
git diff --cached
git diff --cached --check

# 6. 提交并上传
git commit -m "feat: describe the completed change"
git push

# 7. 确认
git status --short --branch
```

一次提交最好只表达一个意图。例如“增加 BusyBox 长稳脚本”和“更新板端测试报告”最好分成
两个提交，便于审查、定位和撤销。

## 7. 常见命令详解

### 7.1 状态和差异

| 命令 | 作用 |
|---|---|
| `git status` | 查看分支、未跟踪、已修改和已暂存文件 |
| `git status --short --branch` | 紧凑显示状态和分支同步关系 |
| `git diff` | 查看尚未 `add` 的变化 |
| `git diff --cached` | 查看下次将提交的内容 |
| `git diff HEAD` | 查看所有尚未提交的变化 |
| `git diff --check` | 检查多余空格等格式问题 |

`git status --short` 的常见标记：`??` 是新文件，` M` 是尚未暂存的修改，`M ` 是已暂存，
`MM` 是暂存后又继续修改，`D` 是删除。

### 7.2 历史

```powershell
git log --oneline --decorate --graph --all
git show HEAD
git show --stat <提交号>
git blame <文件路径>
```

`log` 查看历史，`show` 查看某次提交，`blame` 查看每行最后由哪次提交修改，用于定位历史。

### 7.3 获取远端更新

```powershell
git fetch origin
git pull --ff-only
```

`fetch` 只下载远端信息，不修改工作区。`pull` 会下载并合入当前分支。`--ff-only` 只允许简单
快进，出现分叉时停止，适合新手避免意外合并提交。

工作区有未提交内容时不要盲目 `pull`。可先提交完整工作，或临时使用：

```powershell
git stash push -u -m "临时保存：正在做的工作"
git pull --ff-only
git stash pop
```

`stash` 只是临时区，不是长期备份；重要工作仍应提交并推送。

### 7.4 分支

```powershell
git switch -c feat/busybox-soak
git branch -vv
git switch main
git merge --no-ff feat/busybox-soak
```

小改动可直接在 `main` 做清晰的小提交；大功能、实验或可能失败的工作建议使用分支。分支名
可用 `feat/功能`、`fix/问题`、`docs/主题`。

### 7.5 安全撤销

撤销前先运行 `git status` 和 `git diff`，确认将丢弃什么。

| 情况 | 推荐命令 | 影响 |
|---|---|---|
| 已 `add`，但不想提交 | `git restore --staged <文件>` | 只取消暂存，内容保留 |
| 某文件改乱且确定不要 | `git restore <文件>` | 丢弃未提交修改，通常不可恢复 |
| 已提交但未推送，说明写错 | `git commit --amend` | 重写最新提交，请谨慎 |
| 已推送提交需要撤销 | `git revert <提交号>` | 新建反向提交，保留公开历史 |
| 查看旧版本文件 | `git show <提交号>:<路径>` | 只读，不改变工作区 |

不要把 `git reset --hard`、`git clean -fd` 或强制推送当成日常命令。它们可能永久丢弃未提交
文件或重写公开历史。已经推送的主分支优先使用 `git revert`。

## 8. GitHub 登录与常见错误

### 第一次 push 要求登录

Git Credential Manager 通常会打开 GitHub 授权页。确认登录的是 `cccoddad`。凭据由系统凭据
管理器保存，不应写入项目文件。

### `remote origin already exists`

```powershell
git remote -v
git remote set-url origin https://github.com/cccoddad/rk3568-edge-av-gateway.git
```

### `rejected` 或 `non-fast-forward`

远端含有本地没有的提交。不要强推，先检查：

```powershell
git fetch origin
git log --oneline --graph --decorate --all -20
git pull --ff-only
```

如果 GitHub 建仓时创建了 README、LICENSE 或 `.gitignore`，远端和本地可能是两段独立历史，
应先停下检查，不能随便 `--force` 覆盖。

### `Authentication failed`

确认仓库地址、浏览器登录账号、仓库写权限，以及 Windows 凭据管理器是否残留另一个 GitHub
账号。不要在聊天、截图或日志中发送 Token。若 Token 曾进入 Git 历史，仅删除当前文件不够；
应立即在 GitHub 撤销 Token，再处理历史。

## 9. 本项目路径的特殊注意事项

当前 Windows 项目路径含非 ASCII 字符和一个不可见的零宽字符。尽量在项目目录中使用相对
路径；处理绝对路径时使用完整引号或 PowerShell 的 `-LiteralPath`。构建脚本已用短 ASCII
Junction 规避部分 Ninja/MinGW 路径问题，Git 本身可以正常管理这些文件。

## 10. 提交信息建议

| 前缀 | 用途 | 示例 |
|---|---|---|
| `feat` | 新功能 | `feat: add BusyBox resource sampler` |
| `fix` | 修复 | `fix: stop signal watchdog after child exit` |
| `test` | 测试 | `test: archive RK3568 signal results` |
| `docs` | 文档 | `docs: record board baseline` |
| `build` | 构建系统 | `build: add vendor sysroot toolchain` |
| `ci` | CI | `ci: verify release build` |
| `refactor` | 不改变行为的重构 | `refactor: isolate capture factory` |
| `chore` | 维护 | `chore: establish project baseline` |

提交前确认：是否只做一件事？测试是否清楚？是否混入密码、构建产物、临时调试输出或无关文件？
