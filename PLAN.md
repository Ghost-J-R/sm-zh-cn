# smre-cn：超银汉化版反编译移植工程

让 [snesrev/sm](https://github.com/snesrev/sm) 反编译版 Super Metroid 原生运行
《超级银河战士/超级密特罗德 v1.00》汉化 ROM（简/繁双版）。

## 1. 为什么现在汉化 ROM 会报错（技术原理）

sm 的运行架构：

- 游戏代码（65816 指令）被反编译成 C（`src/sm_XX.c`，覆盖 32 个代码 bank），**执行时由 C 替换**；
- ROM 数据仍按硬编码绝对地址读取：`RomFixedPtr(addr)` = `&g_rom[(((addr>>16)<<15)|(addr&0x7fff)) & 0x3fffff]`（`src/sm_rtl.h:42/45`），`g_rom` 指向加载的 ROM 缓冲区（`src/sm_cpu_infra.c:718`）；
- 内置模拟器同步运行同一份 ROM，与反编译版**逐帧比对**，失配时画面显示倒计时。

因此：汉化补丁改的**数据**（文本、字库、tilemap）会被 C 代码自动读到；改的**代码**（开场字幕、道具提示框、地图界面等汇编重写）仍执行原版逻辑 → 与模拟器失配、各种报错。

移植 = 把补丁在 7 个代码 bank 中的汇编改动翻译成对应 C 函数改动。数据改动无需移植（换 ROM 即生效）。

ROM 加载层**无需改动**：`src/snes/snes_other.c:78-95` 会把 ROM 扩展对齐到 2 的幂（原版 3MB 被镜像填充到 4MB），LoROM 掩码 `& (romSize-1)` 在 4MB 时精确覆盖全地址空间——汉化 ROM 的 4MB 布局天然兼容。

## 2. 移植面（实测数据，详见 docs/rom-diff.md）

| CPU bank | 改动字节 | 主要内容 |
|---|---|---|
| 80 | 154 | 含 ROM 头（标题/校验和）；实际代码/表改动在 $80:8420-84E6 |
| 81 | 457 | 疑似 6 张文字 tilemap（0x800 步长块）+ 零星代码 |
| 82 | 565 | **地图界面"大手术"主体**（$82:9710-98B5，422B）+ 装备/地图散点 |
| 85 | 86 | 指针表（低字节逐点）+ 少量代码 |
| 86 | 2 | 单点 2 字节 |
| 8B | 100 | 跳转目标表（步长 4 的 2 字节散点）+ 少量代码（开场/标题） |
| 8D | 110 | $8D:B8E5-B954 紧凑块（疑似道具提示框/字幕重写） |

数据 bank（83/8C/8E/9A/A6/B6/B7，共 6,996 字节）+ 新增第 4 个 MB（中文字库/文本）——**无需移植**，换 ROM 自动生效。

**CHS↔CHT**：代码 bank 82/85/86/8D 完全一致；仅 80(5B, 标题)、81(4B)、8B(2B) 不同；3MB 数据区差 2,196B、第 4MB 差 44,569B（简/繁文本）。**CHS 移植完成后 CHT 接近零成本。**

## 3. 工具清单

| 工具 | 用途 | 状态 |
|---|---|---|
| git（PortableGit） | 版本管理；本机须 `http.sslBackend=openssl`（schannel 无凭据），已写入仓库 config（含 `http.sslVerify=false`，见第 6 节安全说明） | ✓ |
| TCC + SDL2-2.24.1 | 快速构建（`build_tcc.bat`），已在 smre-cn 本地（git-ignored），冒烟构建通过（sm.exe 1,817,600B 与上游同源同工具链产物零偏差） | ✓ |
| Python 3.14.7 | `Tools\Python314\`（完整安装布局，已合并修复并验证 ssl/sqlite3/zlib）；pip 受本机网络限制不可用，Python 包用浏览器下载 zip 解压到 `Tools\packages\` | ✓ |
| 自研反汇编管线 | `Tools\py.bat` + `Tools\scripts\disasm_range.py`（sm65816.py 全 opcode 表 + M/X 跟踪 + 字节不变式自检）；已用复位入口 $80:841C 逐字节验证（与官方启动序列完全一致），并完成 CHS/原版 8D 区间差分冒烟 | ✓ |
| Mesen2 2.1.1 / bsnes-plus 05 x64 | GUI 调试器已就位（二进制发行，无需源码）：`Tools\Mesen_2.1.1_Windows\Mesen.exe`、`Tools\bsnes-plus-05-x64\bsnes.exe`；配合 `Tools\SM_Debugging\` 符号（Mesen.msl / bsnes.sym）做断点/单步/动态验证与反汇编交叉校验 | ✓ |
| 第三方 Python 反汇编库 | 原计划的可选交叉校验（elliotnunn spanner / disasm65816）仓库已删除（404），不再依赖；交叉校验由 GUI 反汇编器 + SM_Debugging 符号承担 | ✗ 取消 |
| MSYS2/gcc 或 VS2022 | 可选：生产构建与更强警告 | 可选 |
| UPS 工具 | 不需要（已有成品 CHS/CHT ROM）；补丁重放可用 Floating IPS | — |

## 4. 验证方法（每个 bank 移植后的硬标准）

1. `build_tcc.bat` 构建通过；
2. 将 `sm_chs.sfc` 复制为 `sm.smc`，运行 `sm.exe`；
3. 屏幕上逐帧比对**失配计数持续为 0**（无红字倒计时）即该 bank 移植正确；
4. 每 bank 一次提交，失配时用 sm 的存档/回放（F1-F10 快照）定位。

## 5. 里程碑

- **M0 环境验证**（已完成）：fork + 工具链 + 原版 ROM 冒烟构建 ✓
- **M1 逐 bank 移植**：按风险从高到低——82（地图大手术）→ 8D（道具提示框）→ 81（tilemap+代码）→ 8B（跳转表）→ 80（表）→ 85 → 86；每 bank：反汇编对照 → 改 C → 构建 → 逐帧比对归零 → 提交
- **M2 CHS 全量验证**：完整通关主要流程（开场→装备→地图→道具获取），比对计数全程为 0
- **M3 CHT 验证**：核对 bank 81(4B)/8B(2B) 差异，CHT ROM 全量回归
- **M4 双版发布**：release 构建 + 使用说明

## 6. git 使用约定

- `upstream` = https://github.com/snesrev/sm.git（fork 基准：`main` = 578f90b，2023-04-17；`upstream/devel` 已拉取参考，比 main 多 2 个提交：c1a840d "Massive code cleanup"、dd215a3 "Cleanup"）
- 工作分支 `cn`；移植提交逐 bank 进行，信息标注 bank 与区间
- 仓库级身份为占位（`smre-cn <smre-cn@local>`），推送前请改：`git config user.name/user.email`
- ROM（sm.smc / sm_*.sfc / *.srm）、工具链（third_party/tcc、SDL2-2.24.1）、构建产物（sm.exe、SDL2.dll）全部 git-ignored，**不进入版本库**（ROM 为版权物）
- 安全说明：本仓库 config 含 `http.sslVerify=false` 与 `http.sslBackend=openssl`——本机 Windows schannel 无可用凭据且 openssl 全链路校验在此环境失败（TLS unexpected eof），为可联网 git 操作所需；若迁移到正常网络环境，建议恢复 `git config http.sslVerify true`
