# Bank 82 移植分析（首轮，M1 材料）

> 材料来源：`Tools\disasm\diff_bank82.txt`（disasm_patch_diff.py 自动生成，原版 vs CHS 双列反汇编 + 字节不变式自检）；函数地址映射来自 `src/sm_82.c` 的函数注释（`// 0x82xxxx`）与 `assets/names.txt`。
> 验证回路（实测 2026-08-14）：原版 ROM 跑 sm.exe 25 秒零失配快照；CHS ROM 触发 `saves/bug-<ts>.sav`——检测机制对汉化失配工作，可作为每个区间移植后的客观验收。

## 1. 核心判定：422B 大块（$82:F710-F8B5）= 补丁在空白空间新增的代码与跳转表

- **原版该区域是 0xFF 空白填充**（未使用 bank 空间），names.txt 在 F714 之后无任何符号；
- 汉化版在此写入 422 字节新内容，结构如下：
  - `$82:F710-F71F`：8 个 16 位小端指针（F730/F750/F770/F790/F7B0/F7D0/F7F0/F7D0）——**新跳转表**；
  - `$82:F720-F72F`：8 个指针（F740/F760/F780/F7A0/F7C0/F7E0/F800…）——第二张跳转表；
  - `$82:F730 起`：被指目标即新子程序区（线性反汇编呈"29 71/29 72…"形态是因为从表中间开始解码，实为子程序入口 + 数据）；
  - `$82:F880`：已确认的**新地图绘制子程序**——`SEP #$20 / LDA $69 / AND #$01 / CMP #$01 / BEQ …` 检查标志位后调用 `JSL $82BB30`（DisplayMapElevatorDestinations）、`JSR $B9C8`（MapScreenDrawSamusPositionIndicator）、`JSL $82B672`（DrawMapIcons）——即汉化说明中"地图界面大手术"（Sel 键收集数显示）的实现；
  - `$82:F89B`：与 F880 结构对称的第二段（无 ElevatorDestinations 调用版本）。
- **调用点证据**：`$82:9156`（PauseMenu_2 内）原版 `JSL $82BB30` 被改为 `JSR $F880` + `JMP $9161`；`$82:9123` 区域亦有 `JMP $913B` 类改动——补丁改写了暂停菜单到新代码的调用路径。
- **移植结论**：此块必须移植——新增 C 函数（$82:F730+ 各入口、$82:F880、$82:F89B 等）+ 修改 PauseMenu_2 等调用方；数据指针表可经 RomFixedPtr 自动读取，但**跳转表指向的子程序若无 C 函数，运行时会失配**。

## 2. 区间 → C 函数映射表（bank 82 全部改动区间）

| SNES 区间 | 所属 C 函数（src/sm_82.c） | 改动性质初判 |
|---|---|---|
| $82:8F38-8F3C | LoadPauseMenuTilesAndClearBG2（0x828E75）尾部 | 待逐条对照（5B） |
| $82:9123-9127 | PauseMenu_0_MapScreen（0x829120） | 分支重排（BPL/JMP $913B） |
| $82:9156-915B | PauseMenu_2（0x829156） | **JSL $82BB30 → JSR $82F880**（调用新地图代码） |
| $82:91AE-91B0 | PauseMenu_0_MapScreen 内部 | 3B（含边界未覆盖字节，见 diff 文件） |
| $82:9353-9356 | GameState_16_Unpausing_Async（0x829324） | 待对照 |
| $82:93F6-9439 | LoadPauseMenuMapTilemapAndAreaLabel（0x8293C3） | 68B 连续改动，地图区域名相关 |
| $82:9630-9647 | DrawRoomSelectMapAreaLabel（0x829628） | 24B，区域名绘制 |
| $82:AB4A-AB4D | EquipmentScreenSetupReserveMode（0x82AB47） | 4B |
| $82:AC52-AC57 | EquipmentScreenTransferBG1Tilemap（0x82AC22） | 6B |
| $82:AEB5-AEE4 | kEquipmentScreenTilemap_VARIASUIT 数据区（0x82AE8B） | **数据改动，自动生效** |
| $82:B1E6-B1EB | WriteSamusWireframeTilemapAndQueue（0x82B1E0） | 6B |
| $82:EC68-ECB1 | GameOptionsMenu_1_LoadingOptionsScreen（0x82EC11） | 多处 1-2B（语言选项相关） |
| $82:EDF6-EE34 | OptionsMenuFunc4（0x82EDED） | 多处 1-2B（邻近 ToggleJapanese 0x82EDDA） |
| $82:F710-F8B5 | **新增区域（原版为 FF 空白）** | 新跳转表 + 新子程序，见第 1 节 |

## 3. 移植动作清单（M1 bank 82 顺序，按风险从高到低）

1. **F710-F8B5 新块**：以 $82:F730/F740/…/F880/F89B 为入口，把新子程序逐段翻译为 C（入口边界用调用点回溯 + GUI 调试器确认）；新增的跳转表数据保持 RomFixedPtr 读取。
2. **调用点改写**：PauseMenu_2（0x829156）的 `JSR $F880` 语义、PauseMenu_0_MapScreen（0x829120）分支重排——与第 1 步协同完成。
3. **数据区复核**：AEB5-AEE4 确认仅数据变化（无指针位移），无需 C 改动。
4. **其余散点**：按 diff 文件逐区间对照（每个区间 < 70B，多为 1-6B 小改）。
5. **每完成一组改动**：build_tcc.bat 构建 → 以 CHS 为 sm.smc 跑 sm.exe → 断言 saves/ 无新 bug-*.sav（首帧失配即失败，计数归零才算过）→ 恢复原版 sm.smc。

## 4. 已确认的工具与机制事实

- 失配检测：`sm_cpu_infra.c:957-964 SaveBugSnapshot()` 写 `saves/bug-<time>.sav`（首帧失配触发一次），屏幕倒计时 300 帧（main.c:196-197）。
- 双列反汇编：`Tools\py.bat Tools\scripts\disasm_patch_diff.py <orig> <chs>` 可随时重生成全部 7 个 bank 材料；短区间（1-2B）因"不输出半条指令"规则覆盖率不全，材料头行仍含原始文件偏移。
- 函数定位：`src/sm_XX.c` 每个函数注释 `// 0xXXxxxx` 即其 SNES 地址（M1 地址↔C 映射的唯一权威来源，共 312 处/82 文件实测）。
