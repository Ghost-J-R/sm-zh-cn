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
| **$E3:8000** | 0x318000 | **$80:841F JML（复位向量）** | **主体 8000-8043 + 辅助函数 8047-814B（延时/渐亮渐灭/清RAM/清PPU/MVN拷贝），已反汇编定界** | **待移植（最高优先级）** |
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

1. **bank 80 复位钩子（新发现，最高优先级）**：$80:8420 `JML $808423→JML $E38000` + $E3:8000 主体——boot 最前端，第一帧失配的直接来源，不移植则遮蔽全部后续验证；B6ED-B6F3 散点待判定（疑似指针表低字节，B/C 类）；8693 透明（D 类）；
2. **bank 8B 片**：8D23→E38873 **已完成**（Func20 机制全套 + 验证回路对照零回归）；剩余：A3FF→E388CB、9295/9BCA 指针值（CHS=C8F0/CHT=C8F4）、A0E3/A0E9、A48C/A490、B53F-DED6 其余散点（解压源指针与 JSR 改动）；
3. **bank 85 片**：8289/82B8 消息框函数、840C→E387DA、8526→E38707、825A 边框数组（B 类）——覆盖道具提示框（boot 后第一类 UI）；
4. **bank 81 片**：8DFE（LoadInitialMenuTiles）、C5xx-EDxx 六组 tilemap（B/C 类确认后处理）；
5. **bank 82 片**：91xx/93xx/96xx/ABxx/ACxx/B1xx/ECxx 各函数 + F710-F8B5 新块（含 E39290/E392DE 目标）——暂停/地图/装备/选项菜单（需交互验证）；
6. **bank 86/8D**：CB0F 2B、B8E5 spritemap（C 类）。
7. **最终统一验证**：全部完成后 CHS 跑 boot 归零 + 用户手动按 Start 进暂停菜单验证 bank 82 片。

> 注意：boot 归零验证必须等第 1-4 步全部完成（失配在首次分歧即触发）；bank 82 的暂停菜单路径需要交互（用户按 Start），或后续用 sm 的 replay 功能自动化。
