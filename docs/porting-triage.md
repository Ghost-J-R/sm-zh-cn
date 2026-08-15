# 移植面筛选（porting triage）

> 生成方式：`Tools\scripts\triage_patch.py`（解析 7 个 sm_XX.c 的 `// 0xXXxxxx` 函数/数组注释建跨度表，对 docs/rom-diff.md 的全部 202 个改动区间逐段判定）。运行：`Tools\py.bat Tools\scripts\triage_patch.py <smre-cn> --rom <CHS.sfc>`。
> 统计（202 区间）：C内改动 199 / 无C覆盖 1 / 跨界 2。

## 1. 移植判定模型（四类）

| 类别 | 判据 | 处理方式 |
|---|---|---|
| **A. C 代码改动** | 区间落在 C 函数跨度内且原/新字节都是指令（跳板/分支/调用） | 改 C 代码：跳板→直调新 C 函数；分支/调用目标修正 |
| **B. C 硬编码数据** | 区间落在 C 的 `static const` 数组内（数组地址注释命中） | 把数组值更新为 CHS 值（**不是自动生效**——C 编译期常量不会读 ROM） |
| **C. ROM 指针数据** | 区间无 C 覆盖或经 RomFixedPtr 读取 | 自动生效，无需处理 |
| **D. 透明改动** | C 执行不到该指令，或可观察效果为零（有证据链） | 无需处理 |

## 2. 关键区间判定与证据

### A 类（C 代码改动——必须移植）
| 区间 | C 位置 | 补丁动作 | 目标新函数 |
|---|---|---|---|
| **$80:8420-8422** | **复位入口 Vector_RESET（$80:841C 的 JML 操作数）** | **`JML $808423 → JML $E38000`（boot 钩子，优先级最高——遮蔽所有后续验证）** | **$E3:8000 复位钩子（清 RAM/PPU + MVN bank E0/E1→$7F + 一批原版 $80xx 辅助函数，末 JML $808423）** |
| $80:8693-8697 | SaveExploredMapTilesToSaved 跨度内 | REP#$30/LDX#$1FFE → `JSL $83AD70/NOP` | $83:AD70 校验和（见 D 类论证——**已判透明**，例外） |
| $82:9123-9127 | PauseMenu_0_MapScreen | 分支重排 BPL/JMP | — |
| $82:9156-915B | PauseMenu_2 | `JSL $82BB30 → JSR $F880/JMP $9161` | $82:F880 新地图子程序 |
| $82:9353-9356 | GameState_16_Unpausing_Async | 待逐条对照 | — |
| $82:93F6-9439 | LoadPauseMenuMapTilemapAndAreaLabel | 68B 连续 | — |
| $82:9630-9647 | DrawRoomSelectMapAreaLabel | 24B | — |
| $82:AB4A-AB4D | EquipmentScreenSetupReserveMode | 4B | — |
| $82:AC52-AC57 | EquipmentScreenTransferBG1Tilemap | 6B | — |
| $82:B1E6-B1EB | WriteSamusWireframeTilemapAndQueue | 6B | — |
| $82:EC68-EE34 | GameOptionsMenu_1_LoadingOptionsScreen / OptionsMenuFunc4 | 多处 1-2B（语言选项相关） | — |
| $85:8289-828C | WriteSmallMessageBoxTilemap | `LDY #$0000 → JSR $82B8/RTS` | $85:82B8（同函数后段） |
| $85:82B8-82C7 | WriteMessageTilemap | 删 JSR $8136 / JSL $808F0C / JSL $8289EF | — |
| $85:840C-8412 | （跨度内） | `→ JSL $E387DA / JMP $841D` | $E3:87DA |
| $85:8526-8529 | （跨度内） | `→ JML $E38707` | $E3:8707 |
| $8B:8D23-8D27 | CinematicFunction_Intro_Func20 | `PHP/REP#$30/PHX → JSL $E38873/RTS` | $E3:8873 |
| $8B:9295-929A | InitializeIoDisplayLogo_Async | `LDA #$80D8 → LDA #$C8F0`（指针值） | — |
| $8B:9BCA-9BCF | （同前区域另一处） | 同上 | — |
| $8B:A3FF-A40C | CinematicFunction_Intro_Initial | `→ JSL $E388CB` | $E3:88CB |
| $86:CB0F-CB10 | （跨度内） | 2B 待对照 | — |

### B 类（C 硬编码数据——必须同步值）
| 区间 | C 数组 | 说明 |
|---|---|---|
| $85:825A-825D | `kLargeMsgBoxTopBottomBorderTilemap[32]`（sm_85.c:71） | 道具提示框边框 tilemap，CHS 改框体大小的数据源头 |
| $82:AEB5-AEE4 | `kEquipmentScreenTilemap_VARIASUIT[9]`（sm_82.c:2509） | 装备界面 VARIASUIT tilemap |
| $81:C5xx-DDxx 六组（疑似） | LoadMenuTilemap（0x81:B3E2-FFFF）跨度内 | 待逐组确认是硬编码还是 RomFixedPtr |
| $8B:B53F-DED6 步长4散点（疑似） | sub_8BB51E 等跨度内 | 指针表低 16 位改动，待确认读取方式 |

### C 类（自动生效，无需处理）
- $80:C111-C2E6 全区（文本/表数据）、$80:B6ED-B6F3、$80:7FC5-7FD4/7FDC-7FDF（ROM 头）
- $8D:B8E5-B954（道具提示框 spritemap 表，triage 判定"无C覆盖"=1 的那段）
- $82:F710-F71F/F720-F72F 跳转表（若 C 侧不通过 ROM 读表，则随代码移植一并处理）
- 第 4MB 的 E1/E4-E7 字库与图形、E3 数据段

### D 类（透明改动——证据链）
**$80:8693 JSL $83AD70（校验和钩子）**
1. 位置：SaveExploredMapTilesToSaved（0x80:85C6-875C）跨度内，但 C 函数体（sm_80.c:238-248）已用等价的数组拷贝替换了原 asm 的 $70→$7F 复制循环——C 侧不执行该指令序列；
2. 钩子语义：求和 bank E0-E7 的 $8000 起展开式 ADC，与 $81:FF60 的存储值（CHS 写入 CB CB 34 34）比对；匹配→LDX #$1FFE/RTL（回复制循环），失配→JML $8086E3（防盗版画面路径）；
3. 可观察效果论证：校验结果只影响 A；返回后 A 立即被复制循环的 LDA 覆盖；X 两路均=1FFE；复制循环本身不受影响。**合法 CHS ROM 上效果与未打补丁完全一致**；
4. 结论：无需移植。$81:FF60 的 4 字节属 C 类（数据，自动生效）。

## 3. 新增代码分岛清单（bank E3 / 83 / 82 空闲区）

| 入口地址 | 文件偏移 | 调用方 | 规模 | 状态 |
|---|---|---|---|---|
| **$E3:8000** | 0x318000 | **$80:841F JML（复位向量）** | **主体 8000-8043 + 辅助函数 8047-814B（延时/渐亮渐灭/清RAM/清PPU/MVN拷贝），已反汇编定界** | **已移植**：sm_80.c 新增 ResetHook_CHS + 15 个辅助函数（$7F 清零、MVN E0/E1→$7F、4 次 DMA chan0、渐亮渐灭），插入 Vector_RESET_Async 协程头；验证 = stderr 差异捕获对照实验（钩子版 vs 基线版 6,177 行诊断逐行零差异） |
| $83:AD70 | 0x1AD70 | $80:8693（D 类，不调用） | 展开式校验和（AD70-AED3） | 不移植（D 类） |
| $E3:8707 | 0x318707 | $85:8526 JML | 待反汇编定界 | 待移植 |
| $E3:87DA | 0x3187DA | $85:840C JSL | 待反汇编定界 | 待移植 |
| $E3:8873 | 0x318873 | $8B:8D23 JSL | 0x5A 字节（8873-88CA，四块 0x180 拷贝铺满 $7E4000-4600） | **已移植**：CinematicFunction_Intro_Func20 改为按序号索引 E6E237 3 字节长指针表；10 个调用点改传序号 1-10 并删第二次调用（Func18 未改，传 0xD389 乱索引与 ROM 一致） |
| $E3:88CB | 0x3188CB | $8B:A3FF JSL | 待反汇编定界 | 待移植 |
| $E3:9290 | 0x319290 | $82:F876/F87A JSL（新地图代码） | 待反汇编定界 | 待移植 |
| $E3:92DE | 0x3192DE | $82:F87A JSL | 待反汇编定界 | 待移植 |
| $82:F730-F8B5 | 0x17730 | $82:9156 JSR $F880；F710 跳转表 | 422B | 待移植 |
| $E3:928A 起连续区 | 0x31928A | 约 0x6DBA 字节 | 最大代码岛（中文文本引擎） | 待分片移植 |

## 4. 移植顺序（按 boot 路径可达性 + 风险）

1. **bank 80 复位钩子**：**已完成**（$E3:8000 全套翻译 + 对照实验零偏差）。B6ED-B6F3 散点待判定（疑似指针表低字节，B/C 类）；8693 透明（D 类）；
2. **bank 8B 片**：8D23→E38873 **已完成**（Func20 机制全套）。**已移植（2026-08-15）**：5 处解压源散点（Func54/BlackoutFromCeres 96D10A→E78080、Func86 96EC76→E79CC0、EscapeFromCeres/Func126 97E7DE→E6EFB5，字节对照实证；运行时验证待过场可达）；9295/9BCA（logo/标题解压源 80D8→C8F0，CHT 为 C8F4）；8BCCD7 判定 C 类（cinematic 指令流经 RomPtr_8B 读取自动生效）；**A3FF→E388CB 已移植**（Intro_Initial 解压2 替换为 ResetHookFill7F8000_88CB 填 $7F8000 起 0x900 字节 FF00，解压3 保留）；**A48C/A490 已移植**（unk_8BA489 DMA 源 7F8000→E6E255、0x900→0xD00 字节）；**A0E3/A0E9 判定：数据表改动（HDMA 链 0x8103→0xF480）、无 C 消费者、25 秒 boot 零差异实证——不改代码，运行时验证待标题交互阶段**。**已知待办**：9295/9BCA 修复后帧 0/151 出现"解压时机错位"差异（theirs=0——模拟器侧该帧未解压、C 侧一帧内推进多帧逻辑；钩子禁用实验证明与钩子无关），需在后续增量中调查 sm 帧对齐机制。bank 8B 全部项已处理完毕。
3. **bank 85 片**：**已完成（2026-08-15）**：825A/8289（WriteLarge/WriteSmall 头尾边框拷贝删除→只调 WriteMessageTilemap；**triage 原判 825A 为 B 类数组有误——实为 A 类函数头代码，kLargeMsgBoxTopBottomBorderTilemap 数组在 ROM $858000 区且 CHS 未改**）；82B8（WriteMessageTilemap→MsgBox_CHS_8774：中文 tilemap 经 E6A35E 3 字节指针表加载）；835B（SetupPpuForActiveMessageBox 尾部→MsgBox_CHS_8527：880D/853A/8615 三段 E6 表 VRAM DMA）；840C（DrawSpecialButton→MsgBox_CHS_87DA 四边角 tile）；8439（小消息框参数 448→416）；8526（ToggleSaveConfirmationSelection 源→E6:A4F7）；93CF-94A7 判定：tilemap 数据表无 C 消费者（文档化，运行时验证待消息框交互）。验证：diag_85 与基线 6,282 行零差异（消息框路径 boot 不可达）。**E3:8707 死分支（CPY #$0040/fallthrough）未翻译**（8526 调用点 Y 恒 0x80，注释备查）。
4. **bank 81 片**：**已完成（2026-08-15）**：8DFE-8E0C（LoadInitialMenuTiles 第二组寄存器写被 `JSL $E383DC` 替换，反汇编定界 83DC-8443 RTL：两段 DMA（$B6AE00→VRAM 0x4300 0xE00、$E7C310→VRAM 0x4A00 0x5E0）+ 重建 VMADDL=0/VMADDH=0x30/VMAIN=0x80）→ C 已插入两 DMA 块；A7BD/AAEB（`LDA #$4154→#$4118`，JSL $829628=DrawRoomSelectMapAreaLabel 的指针参数）→ C 两处调用点 +170→+140（sm_81.c FileSelectMap_5/FileSelectMap_7）；B35B-B365（NewSaveFile 内 japanese_text_flag 0→1，CPX $09 为占位）→ C `japanese_text_flag = 1`；C5xx-EDxx 六组 tilemap 判定：**C 类**（grep 全 src 无引用、ida_types.h 常量最大 0xBF1A 不落入差异区、ROM 16 位引用扫描原版/CHS 位置完全一致，数据换 ROM 自动生效），运行时验证待 file select 菜单交互。
5. **bank 82 片**：**片 A 已完成（2026-08-15）**：PatchBytes 冲突处理（0x82896B/0x82F70F 两 patch 加 `g_rom[0x17710]==0xFF` 条件——CHS 的 $82:F710 起是 8 项指针跳转表，无条件 patch 会破坏表第一项；禁用后模拟器侧跑原版 JSR ($8981,X)，与 C 侧 RunOneFrameOfGameInner switch 分派等价，$82:8981 表 CHS 未改已字节实证）；PauseMenu_0_MapScreen 缩减为仅 pause_screen_mode=0（CHS JSR $F810 空操作+JMP $913B=LDA #0/STA $0763/RTS）；PauseMenu_2 三绘制调用外包 if((reg_TM&1)==1)（CHS JSR $F880）；DrawPauseMenuDuringFadeout else 分支外包 if((reg_TM&1)==1)（CHS JSR $F89B/RTS）。**片 B 已完成（2026-08-15）**：**PauseMenu_3 修正**（$82:91AE 实为 `JSL $E383BF` 4 字节——reg_TM|=0x11 + JSL $E39290 + 电梯显示保留，片 A 的"删调用"误读已更正）；LoadPauseScreenBaseTilemaps 第三个 MDMAEN 后插 E388DD 链（**归属修正：8F38 在 LoadPauseScreenBaseTilemaps(0x828EDA) 内，非 triage 原判的 LoadPauseMenuTilesAndClearBG2**；清零 $9B-$A2 + E38969/E38985/E389B5 地图收集统计（$E39031 掩码/$E39039 键/$E390D9 索引三表）+ E388FA 六字区域标签色（$7E09E0/09DE/09DC 键）；LoadPauseMenuMapTilemapAndAreaLabel 1554-1566 段替换为 E38483 DMA（B6AE00 0x1200→VRAM 0x1700）+ E38510 两行上传（F710/F720 表→VRAM 0x388C/0x38AC 各 0x10 字节）；DrawRoomSelectMapAreaLabel 替换为 E38444 两行 8 字中文区域名（(v&0xEFFF)-0x40，M=16 对齐破解）；EquipmentScreenSetupReserveMode 前插 E38352（$C4=0/$C6=FFFF + DMA B6A800 0x600→0x1400 + E7AD10 0x1200→0x1700）；EquipmentScreenTransferBG1Tilemap 尾插 E3820A 条件缓存（装备类别低字节→$C6、$C4=CD）；WriteSamusWireframeTilemapAndQueue 插 E38217 条件拷贝（E4E633/E4E645 各 9 字节→$7E3D84/3DC4）。**死代码判定**：$82:F816-F87E 与 E39199/E391C7/E392DE 无调用者（CHS 全文搜索零命中）不翻译；**E39290/E391B0 为活代码**（E383BF 调用）已随 PauseMenu_3 移植；$82:AE8A 孤儿函数（原版 ROM 全文 JSR 零命中）内的 AEB5/AEDF→E383D0 判 **D 类透明**（"0x82AE8B=kEquipmentScreenTilemap_VARIASUIT"为上游注释偏差，AE8A 起实为无调用者函数）。**片 C 已完成（2026-08-15）**：EC68/EC8A/ECAC（GameOptionsMenu_1 的 5 段解压中第 1/3/5 段源改中文数据：$97:8DF4→$E6:FB0E、$97:91C4→$E6:F77C、$97:953A→$E6:F953——diff 的"INC $85/PHA"为错位解码，实为 `LDA #$E600/STA $48 + LDA #$FB0E/STA $47` 的指针赋值）；EDF6/EE28（OptionsMenuFunc4 两分支第一个 OptionsMenuFunc5 的 LDY #$0018→#$0032）。运行时交互验证待最终统一验证（用户按 Start 进暂停菜单）。**bank 82 全部区间处理完毕**。
6. **bank 86/8D**：CB0F 2B、B8E5 spritemap（C 类）。
7. **最终统一验证**：全部完成后 CHS 跑 boot 归零 + 用户手动按 Start 进暂停菜单验证 bank 82 片。

> 注意：boot 归零验证必须等第 1-4 步全部完成（失配在首次分歧即触发）；bank 82 的暂停菜单路径需要交互（用户按 Start），或后续用 sm 的 replay 功能自动化。

## 5. 验证方法沉淀（2026-08-15 复位钩子增量实证）

**失配诊断双写（2026-08-15 修正，替代此前误加的弹窗方案）**：失配行为与 sm-main 完全一致——`printf("Verify failure!")` 照常显示在 cmd 窗口、`SaveBugSnapshot()` 照常保存 `saves/bug-<时间戳>.sav`、窗口标题照常加 " | BUG FOUND!"，无任何额外弹窗（用户澄清：所说的"弹窗"即 cmd 控制台窗口本身）。新增：sm 启动时打开 `saves/sm-diag.txt`（"w" 截断 + `_IONBF` 无缓冲），`VerifySnapshotsEq` 的 12 处诊断打印经 `DIAGF` 宏双写（stderr 原样 + 文件副本）——**diag 文件 = 纯 stderr 副本，与既有基线（diag_85 等）可直接逐行对比**，自动验证不再需要 cmd 重定向。注意：`Verify failure!`（stdout）与快照保存日志（stdout）不进 diag 文件（保持基线一致性，失配帧信息由 @N 头行承载）。**当前基线**：`Tools\disasm\diag_baseline.txt`（6,302 行完整版，@0-@151 共 304 块；旧 diag_85.txt 的末块因 cmd 重定向 stderr 强杀截断少 20 行，内容实质一致）。实测：stderr 重定向副本与 sm-diag.txt 前 6,281 行逐行一致，唯一差异为 stderr 强杀截断尾巴——证明双写正确且文件版更完整。

1. **快照 total_frames/PC 不反映失配点**：SaveBugSnapshot 在 RestoreSnapshot(&g_snapshot_before) 之后调用，快照保存的是帧开始状态（PC=复位向量恒成立）。失配定位必须用 **stderr 差异字节捕获**：`cmd /c "sm.exe > diag.txt 2>&1"`（DSH 沙箱禁止命名管道，PowerShell `-RedirectStandard*` 与 `|` 捕获均不可用，必须走 cmd 文件句柄重定向），读 `@N: Memory compare failed` 块的差异地址列表（每帧最多 256 字节，按 RAM 地址升序）。
2. **增量零回归标准**：目标增量移植后，"钩子版 diag" 与 "基线版 diag"（git stash 回退该增量）逐行对比零差异 = 该增量未引入任何新失配（差异可能仍存在，但全部来自其他未移植点）。
3. **归档证据**：`Tools\disasm\diag_hook_f0.txt` 与 `diag_baseline_f0.txt`（各 182,304B，6,177 行，逐行一致）。
4. **更正**：上一轮（Func20 增量）验收报告曾以"快照 PC=00:841C"论证失配在复位路径——该解读依据错误（见本条 1），上轮真实证据为"基线与增量快照逐字段一致"（零回归），该结论仍成立。

## 6. 开场图缺失与帧 0 时序待办（2026-08-15 调查）

1. **sm.exe 窗口无开场图（"汉化 by XXX"）是架构效应，不是移植错误**：sm 模拟器侧每帧 = RunCpuUntilPC 从当前 PC 执行到 WaitForNMI(0x808343)/WaitForLagFrame(0x85813C) 停机点（无 cycle 限制、期间不渲染），帧末 DrawPpuFrameWithPerf 才渲染一次。CHS 复位钩子的闪屏（渐亮→2.4 秒延时→渐灭）全部压缩在帧 0 内，帧 0 末 INIDISP=0（渐灭后）→ 窗口帧 0 全黑，帧 1 起直接是 Nintendo logo。真实模拟器/SFC 上闪屏跨 ~144 帧正常显示。**比对范围（RAM/VRAM/OAM）不受影响**（闪屏数据被钩子自清理，两边一致）。若未来要求闪屏可见：需把钩子渐亮/延时/渐灭改为 COROUTINE_AWAIT 逐帧建模 + 模拟器侧 PatchBytes 对齐。
2. **帧 0/151 解压时机错位（theirs=0）待办**：9295 修复后，帧 0（131 差异字节）与帧 151（132 差异字节，标题加载帧）的差异形态为"mine=C8F0 解压输出、theirs=0"（C 侧早一帧写、模拟器侧该帧未解压），帧 1+ 收敛。4 组实验（散点零影响/钩子禁用/回退复现/解压输出验证）排除了解压源、钩子、散点三个假设。**两个可执行调查方向**：(a) 在 sm_cpu_infra.c 的 RunCpuUntilPC（917-929 行）返回处临时插桩打印停机点 addr，对比 9295 修复前后帧 0 的模拟器侧停机点；(b) 组合 --debug（main.c:327，触发失配帧重试）+ debug_cycles（sm_cpu_infra.c:919，打印 CPU 状态）观察帧 0 执行轨迹。最终统一验证（boot 归零）前必须闭环。
