# 统一验证记录：交互游玩暴露的七处 CHS 移植缺陷与修复

> 提交：`185df46`（Bug 1-3）、`fa5ad00`（Bug 4）、`4e8603a`+`25f1136`（Bug 5-6）、`989583f`（消息框崩溃）、`8f4d709`（Bug 7）
> 验证日期：2026-08-15 ～ 2026-08-16
> 结论：标题 → 存档选择 → 选项菜单 → 开场文字 → Ceres 飞船/逃生 → Zebes → 暂停地图 → 变形球消息框 → 撤离开火，各场景经快照复现或实测验证，最终回归 **零失配**。
>
> 约定：使用中文思考，思考时，推理时使用中文（见 readme.md）。

---

## 1. 背景与测试机制

汉化版 ROM（CHS，CRC32 `84D1428A`）在 snesrev/sm 反编译版中运行时，每个游戏帧由两套实现各跑一遍：

- **theirs**：模拟器侧（65816 原生执行 ROM，`RunOneFrameOfGame_Emulated`）
- **mine**：C 侧（`RunOneFrameOfGame`，即移植代码）

帧末对 RAM（7E/7F 共 0x20000）、SRAM、VRAM、OAM 逐一比较。失配时行为与 sm-main 一致：cmd 窗口打印 `Verify failure!` + 自动回退到模拟器侧状态 + 保存 bug 快照（`saves/bug-<时间戳>.sav`，节流 5×60 帧）+ 继续运行；诊断文本经 `DIAGF` 双写 stderr 与 `saves/sm-diag.txt`。

**启动方式**：`Start-Process sm.exe -WorkingDirectory <smre-cn>`（直接启动，cmd 窗口可见报错）。CHS 换入 sm.smc 后测试，测试毕恢复原版（CRC32 `D63ED5F8`）并清理 saves/。

**测试入口条件**：saves/sm.srm 置为"无效存档槽位对"（`0x1FEC/0x1FEE = 0000/0000`，即 8 字节全零的 SRAM），使游戏进入"新游戏"流程（存档选择 → 选项 → 开场）。

---

## 2. Bug 1：存档选择→选项菜单，一行 13 字缺 0x0400 高亮调色板

### 现象

- 帧 1293（用户按 Start 进存档选择后进入选项菜单时）：RAM `7E:32E0-7E32FA`（tilemap 索引 368-380，13 个字）+ 对应 VRAM `0x5170-0x517C` 失配。
- `mine = glyph | 0x0000`，`theirs = glyph | 0x0400`——**13 个中文字形完全相同，只差 0x0400 高亮调色板位**。
- 字形序列：`000F 0057 012E 012F 017F 005C 005D 005E 005F 012A 012B 0058 000F`（CHS 中文字库索引）。

### 检查方式

1. sm-diag.txt 给出失配帧号、地址、mine/theirs/prev 三列值；
2. 在 `OptionsMenuFunc5`（0x82ED28，函数体 = `tilemap[k..k+j] = a | (tilemap[k..k+j] & 0xE3FF)`，正是"给一行加调色板位"的例程）加临时入口打印，抓到了帧 1293 的调用参数：`(a=0x400, k=0x288, j=0x32)`、`(a=0x400, k=0x2C8, j=0x18)`、`(a=0, 0x348, 0x32)`、`(a=0, 0x388, 0x32)`；
3. 反汇编 CHS ROM `$82:EDED`（OptionsMenuFunc4）+ 与 ORIG 逐字节对照。

### 根因

CHS 把 OptionsMenuFunc4 内**四处** OptionsMenuFunc5 调用的 `LDY #$0018` 全部改为 `#$0032`（中文行更长，0x2C8 行须覆盖到 tilemap 索引 380）。早期移植只改了每个分支的**第一处**调用，两处第二调用仍是 0x18：

- C 侧高亮覆盖 tilemap 索引 356-367；
- ROM 侧覆盖 356-**380**；
- 差值 368-380 恰好 13 个字 = 失配范围，逐字吻合。

### 修复

`src/sm_82.c` OptionsMenuFunc4（0x82EDED）两个分支的第二处调用 `j` 由 0x18 改为 0x32，并更新注释为四处 LDY 全部 #$0032。

---

## 3. Bug 2：开场文字帧，DP `$CE` 差 1 字节（bank 字节副作用）

### 现象

- 帧 3582（开场故事文本阶段，`CinematicFunc_Intro_WaitForFadeinSleep` 执行帧）：RAM `7E:00CE` 单字节失配，`mine=00`、`theirs=E6`。
- `0xE6` 是 CHS 数据 bank 号——ROM 侧把某个 24 位指针的 bank 字节写进了 DP。

### 检查方式

1. 解析 bug 快照定位帧上下文：`game_state=0x1E`（开场电影）、`cinematic_function=0x8BA84A`；
2. 在 snes.c 写路径加监控（CPU 分支 + **bank 0x00 的 RAM 镜像分支**——DP 写 `STA $CE` 实际走 `0x00:00CE` 镜像路径，只挂 7E/7F 分支会漏），实测捕获：**pc=E3:888C、rv=1（模拟器侧）、val=0xE6、帧 1676/3085/3892**；
3. 反汇编 CHS `$E3:8873`（Func20 的 CHS 钩子）：`LDA $E6E237,X / STA $CC`（M=16 写 $CC/$CD）+ `LDA $E6E238,X / STA $CD`（M=16 写 $CD/$CE）+ 帧末 `STZ $CA / STZ $CC`。

### 根因

C 移植 `CinematicFunction_Intro_Func20`（sm_8b.c:774）用 C 直指针完成了拷贝，**跳过了钩子的 DP 指针副作用**。ROM 侧帧末残留 `$CE = 指针 bank 字节（0xE6/E7）`、`$CA-$CD = 0`；C 侧 `$CE` 保持 0x00 → 帧末快照差 1 字节。

### 修复

`src/sm_8b.c` Func20 补 4 条等价副作用（与 ROM 指令一一对应）：

```c
*(uint16 *)(g_ram + 0xCC) = e[0] | ((uint16)e[1] << 8);   // STA $CC（M=16）→ $CC/$CD={lo,hi}
*(uint16 *)(g_ram + 0xCD) = e[1] | ((uint16)e[2] << 8);   // STA $CD（M=16）→ $CD/$CE={hi,bank}
// ...四个 0x180 字节拷贝块（既有 memcpy，顺序不变）...
*(uint16 *)(g_ram + 0xCA) = 0;                             // STZ $CA
*(uint16 *)(g_ram + 0xCC) = 0;                             // STZ $CC（M=16 → $CC/$CD 清零，$CE 保留 bank）
```

---

## 4. Bug 3：飞船场景解压源地址抄写错误（0xE78080 → 0xE78000）

### 现象

- 帧 9706/8129/8350（用户流程进入 Ceres 飞船场景，`CinematicFunction_Intro_Func54` 执行帧）：RAM `7E:15000` 起 **13133 字节**失配，`mine=真实解压数据`（`34 00 0B 18...`）、`theirs=0000`；VRAM 同步失配。
- 帧号随用户操作节奏漂移（7762-9706），失配本身确定性发生。

### 检查方式

1. 失配帧上下文解析：`game_state=0x1E`、`cinematic_function=0x8BBCA0`（Func54）；
2. C 侧解压调用打点：帧内 `95A82F→7E:10000`、`96FE69→7E:14000`、`E78080→7E:15000` 三次（前两块无失配，仅第三块失配）；
3. 模拟器侧写路径监控（snes_write 内打印解压器 pc 区间的每次写：dst bank、地址、值、当前源指针 $47/$48/$49），实测失配帧铁证：

   ```
   [DECWR2] f=200 ... 4E=7F adr=7F:5000 val=00 src=E7:8003
   ```

   即 ROM 侧**从 `E7:8000` 起读**（写发生时 $47 已推进 3 字节），输出全零；C 侧从 `E7:8080` 起读，输出真实数据；
4. ROM 调用点原始字节核对（`$8B:BCDE` 与 `$8B:C178`）：

   ```
   A9 00 E7  85 48      ; LDA #$E700 / STA $48  → bank=E7
   A9 00 80  85 47      ; LDA #$8000 / STA $47  → 低 16 位 = 0x8000
   22 FF B0 80          ; JSL $80B0FF（解压）
   00 50 7F             ; 内嵌 dst = 7F:5000
   ```

   源地址 = **0xE78000**；ORIG 对应字节为 `A9 00 96 / A9 0A D1`（源 0x96D10A），CHS 改为 E7 后低 16 位是 `8000`。

### 根因

早期移植把 CHS 改动注释写成"源 0x96D10A→0xE78080"，**把 `#$8000` 误抄成 0x8080**（多打一个 8）。C 侧读 `E7:8080`（有效压缩流，解压出真实图形），ROM 侧读 `E7:8000`（另一段数据，解压输出不同），帧末 0x600 字节区域大面积失配。

### 修复

`src/sm_8b.c` 两处（`CinematicFunction_Intro_Func54` 3518 行、`CinematicFunctionBlackoutFromCeres` 3786 行）：

```c
DecompressToMem(0xE78000, g_ram + 0x15000);  // 原误抄 0xE78080
```

---

## 5. Bug 4：Ceres 逃生 Mode7 旋转窗口内 BeamExplosion 特效画到 (0,0)

> 提交：`fa5ad00`（src/sm_93.c）

### 现象

帧 17767（Ceres 逃生 Mode7 旋转窗口内开火）起 OAM 条目 17-20 逐帧失配：mine 把 BeamExplosion 特效投射物（type 0x8700 = 0x8000|kProjectileType_BeamExplosion）画到屏幕 (0,0)，theirs 为旋转后的正确坐标。

### 根因

`DrawBombAndProjectileExplosions`（0x93834D，管投射物槽 5-9）漏译 `CalcExplosion_Mode7`（$8B:8AD9）的返回值：ROM 侧 JSL 后用 $12/$14 作绘制坐标，C 侧丢弃 `Point16U` 返回、读未赋值的局部 r18/r20(=0)。这是**上游 snesrev/sm 578f90b 的潜伏缺陷**，与 CHS 无关——sm-main 与原版 ROM 未暴露只因该输入组合（旋转窗口内绘制爆炸特效）未被其测试命中。

### 修复

按 ASM（JSL $8B8AD9 → LDA $12）接回返回值：`v2 = r18 = pt.y; r20 = pt.x;`。快照 bug-1786784933.sav 复现验证零失配。

---

## 6. Bug 5：暂停地图主循环误判 + stub 冲突与加载崩溃（F71x 专项说明）

> 提交：`4e8603a`（暂停地图移植补全 + stub 重定位）、`25f1136`（垫片移除）
> 另：`989583f`（消息框 cart_readLorom 崩溃，变形球道具框）——见 §7

### Bug 5a：暂停地图 $82:F810 链误判

早期移植把 $82:F810 链判为 no-op，实际是补丁新增的完整地图主循环（Select 切换收集率标签 + reg_TM&1 条件绘制 + LR/Start/滚动/标记/图标/电梯）。反汇编实证后逐条译回 C：`CHS_MapScreen_MainLoop`($82:F815)、`CHS_MapScreen_LabelPaletteUpload`($E3:9199)、`CHS_MapScreen_LabelUpload`($E3:91C7)、`CHS_MapScreen_LabelWrite_92C1`($E3:92C1)、`CHS_MapScreen_LoadGfx_DMA`($E3:9290)、`CHS_MapScreen_QueueSelectSfx`($E3:92DE→QueueSfx1_Max6(0x38))。

### Bug 5b：stub 重定位与加载崩溃（F71x 专项说明）

上游 "Fix so main code is in a function" 的 stub 原在 $82:F713——CHS 在 $82:F713-F722 放了跳转表/标签表数据，stub 与之冲突：加载存档后 RTL 返回地址（旧 stub 压栈的 F718/F71B/F71F）落进数据区 0x82F720（标签表数据 `40`=RTI）→ cart_readLorom 崩溃（PC history 恒为 `…0x80834A → 0x82F720`）。

处理链：
1. stub 重定位到 $82:F8B6（两 ROM 空白区，跳转表/标签表零占用），8944 补丁与 LoadSnesState 重定向常量同步为 F8B6/F8BD；
2. **F71x 是什么**——旧 stub 的三个返回点：F718（JSL $809459 返回）、F71B（JSR $8948 返回）、F71F（JSL $808338 返回）；
3. **为什么新存档免疫**——stub 迁 F8B6 后压栈值只有 F8BB/F8BE/F8C2 系，F713-F722 是纯 ROM 数据区、没有任何代码路径压入这些值；栈返回平移垫片按用户决定直接移除（`25f1136`）；
4. **运维约定**——旧 stub 时代的 15 份 bug-*.sav 快照作废删除（sm.srm 不受影响），AUTOLOAD 复现一律用新布局快照。

---

## 7. Bug 6：消息框崩溃（cart_readLorom 读 0x2001）+ 9 处补丁/转录分歧

> 提交：`989583f`（src/sm_cpu_infra.c、src/sm_85.c）

拿到变形球弹道具框即崩溃：`While trying to read from 0x2001`，PC history 0x8582BC-C3。

### 根因

上游 11-NOP 补丁按**原版**布局对齐 8582B8（原版 = JSR $8136/JSL $808F0C/JSL $8289EF），CHS 实际布局为 JSL $808F0C/JSL $8289EF/**JSL $E38774**/JSR $8136/RTS——11 个 NOP 吃掉 JSL $E38774 前 3 字节，残字节 0xE3 被当作 opcode SBC ($20,S),Y，垃圾栈指针间接读落到 0x2001 → Die。

### 修复

- sm_cpu_infra.c：8582B8 改 8 NOP + 保留 JSL $E38774 + 3 NOP + 保留 RTS；85835A 改 4 NOP（保留 JSL $E38527 中文图形上传）；新增 E3 内部补丁 5 条（E38527/E38774 头尾的帧等待/音乐/SFX 调用，C 转录省略故 emulator 侧同步跳过）；
- sm_85.c 六处转录副作用镜像：`message_box_das0l_value=0x180`（原来从未赋值，C 侧 DMA 会是 0 字节）、8774/853A/8707 的 `$CC/$CD` 清零与 `$CE=e[2]`（LDA [dp] 24 位指针的 bank 分量）、8615 的 B5 表常量索引 `$C8>>1`、8707 补上可达的 Y=0x40 分支（E6A577 高亮，原注释误标死代码）、87DA 传参 0x0C/0x0E（原来 ×256 误读）。

---

## 8. Bug 7：Ceres 逃生旋转窗口内开火，光束投射物 Y=0xFC 失配

> 提交：`8f4d709`（src/sm_93.c，1 行修复 + 4 行注释）

### 现象

帧 15922 起成组失配（组间空档 = 未开火）：每帧 1 字节 RAM + 1 个 OAM 字。失配地址 0x3B5/0x3C5/0x3CD/0x3D1/0x3D5/0x3D9/0x3DD/0x3E1/0x3E5/0x3E9/0x41D/0x421 全部是 OAM 条目 17/21/23-30/43/44 的 **Y 字节**，mine 恒为 0xFC（= -4，屏外），theirs 为旋转后的屏内值，X/tile/attr 全一致。

### 定位过程（AUTOLOAD + 分层插桩）

1. bug-1786850061.sav（帧前 theirs 状态：槽 1 type=0x8000 普通光束、ceres_status=0x0002）→ save0.sav，帧 200 自动加载把失配钉死；
2. OAM ycoord 写入点 0xFC 条件打印（T1-T7）→ 抓到 `DrawGrappleOrProjectileSpritemap`（$81:8A5F）写 idx=0xAC（条目 43）yin=0 off=0xFC；
3. 入口打印（TB/TP）→ 锁定 `DrawProjectileSpritemap k=2`（槽 1）；
4. 输入输出打印（CM7/P2）→ `CalcExplosion_Mode7` 实输出 y=0x98（正确，theirs=0x94 差 4 为帧内位移），而 `DrawPlayerExplosions2` 内 r18=0——**检查用的 v3 通过 0xFF00 越界检查后，绘制传入的是未赋值的 r18**。

### 根因

`DrawPlayerExplosions2`（0x938254，管投射物槽 0-4）Mode7 分支：

```c
Point16U pt = CalcExplosion_Mode7(v0);
v3 = pt.y;      // 只接了检查值
r20 = pt.x;
// r18 漏接 → 保持 0 → 绘制 y = 0 + spritemap 偏移 0xFC(-4) = 0xFC
```

ROM $93:82B3 为 `JSL $8B8AD9 → LDA $12`（$12=y）→ $82CD 检查 $12 → $818A4B 用 $12 作绘制 y。这是 **fa5ad00（Bug 4）同款"丢弃 Mode7 返回值"缺陷在另一个函数的残留**：fa5ad00 修了 DrawBombAndProjectileExplosions（槽 5-9），本函数（槽 0-4）漏修，此前验证的帧 17767 场景未触发它。

### 为什么原版测试多次没有这个失配（用户重点疑问）

1. **bug 在 C 移植层，与 ROM 无关**：失配是 mine（snesrev 的 C 移植 sm_93.c）vs theirs（ROM 执行）之间的差异；实测原版 ROM 与 CHS ROM 在 bank 93（含 $93:8254/$93:834D）**逐字节相同（0 diff）**——"汉化没改这里"完全正确；
2. **触发窗口只有 1-2 帧**：失配要求"ceres_status 置 0x8000（旋转启动）的瞬间，槽 1 有存活的光束投射物"——快照实测失配发生在旋转启动边界（帧前 ceres_status=0x0002、下一快照已是 0x8000 且槽清空）；开火帧与旋转启动帧的相位由输入序列决定；
3. **原版测试没踩中 = 开火时机没落在窗口**，与 ROM 内容差异无关——同一引擎 + 原版 ROM，在同样的开火时机下同样会失配（sm-main 亦然，fa5ad00 注释已记"该输入组合未被其测试命中"）。汉化版其他 bank 的改动（如 8B 电影文本函数）会平移帧时序相位，使"玩家自然的开火时机"恰好与旋转启动同帧的概率改变。

### 修复

```c
Point16U pt = CalcExplosion_Mode7(v0);
v3 = r18 = pt.y;   // 与 fa5ad00 的 v2 = r18 = pt.y 同款双赋值
r20 = pt.x;
```

### 验证

- AUTOLOAD bug-1786850061.sav 帧 200：修复前 mine=0xFC vs theirs=0x94 必失配；修复后 r18=0x98、帧 200/202 起零失配；
- 排查中排除项（有证据）：ROM $818AB7 是 RTL 空桩（C 侧"漏分支"实为正确）；ClearUnusedOam 清零值为 0xF0；CalcExplosion_Mode7 全 3 个调用点（sm_93.c×2、sm_9b.c×1）已核对无其他同款缺陷——槽 0-4（本修复）+ 槽 5-9（fa5ad00）全闭环；
- 10 处临时插桩全部移除（git diff 仅剩 sm_93.c 一处修复），14 秒启动冒烟 0 字节。

---

## 9. 验证方式与结果

| 验证项 | 方法 | 结果 |
|---|---|---|
| Bug 3 修复 | bug 快照复制为 `saves/save0.sav`，临时在 main.c 帧 200 自动 `RtlSaveLoad(2,0)` 加载，30 秒内复现原失配帧 | 修复前必失配；修复后 **帧 200 零失配** |
| Bug 1/2 修复 | 同一快照场景（状态 = 选项菜单→开场→飞船前）逐帧对比 | 零失配 |
| Bug 4 修复 | bug-1786784933.sav AUTOLOAD 帧 200 | 修复前必失配；修复后零失配 |
| Bug 5 修复 | bug-1786796724.sav AUTOLOAD 加载标记确认 + 60 秒存活 + 纯启动回归 | 零失配零崩溃、diag 0 字节 |
| Bug 6 修复 | 14 秒启动冒烟 + 8 个补丁地址常量二进制校验 | 进程存活、diag 0 字节、常量全部命中 |
| Bug 7 修复 | bug-1786850061.sav AUTOLOAD 帧 200 双向验证（修复前复现/修复后复测） | 修复前 mine=0xFC vs theirs=0x94；修复后 r18=0x98、零失配 |
| 启动回归 | 60 秒无人值守 boot（标题 + 开场） | sm-diag.txt 0 字节 |
| 最终手动回归 | 用户完整流程：标题 → 存档选择 → slot 1 → 选项菜单 → 开场文字 → 飞船场景 → Zebes → 暂停地图 → 变形球消息框 → 撤离开火 | **零失配**（0 字节 diag） |

---

## 10. 本次调查沉淀的可复用工具与陷阱

### 6.1 bug 快照（.sav）解析格式

失配快照 = StateRecorder v2 格式，解析偏移经历了三次修正，最终正确布局：

```
文件 = 头(16×uint32 = 64 字节)        // hdr[0]==2 才是 v2
     + log(hdr[2] 字节)
     + base_snapshot(hdr[5] 字节)
     + arr(hdr[6] 字节)              // SaveSnesState 输出
arr 内部顺序（snes.c snes_saveload）：
  cpu(27B) → apu → dma → ppu → cart SRAM(8192B)
  → Snes 结构块(51B，hPos..openBus) → ram(131072B) → ramAdr(4B)
ramOff = 64 + hdr[2] + hdr[5] + hdr[6] - 4 - 131072
sramOff = ramOff - 51 - 8192
```

常用字段：`game_state=ram+0x998`、`menu_index=ram+0x727`、`cinematic_function=ram+0x1F51`、`japanese_text_flag=ram+0x9E2`、CPU 的 pc/k/db 在 arr 偏移 8-13。**验证偏移的可靠锚点**：SRAM 槽位对 `0x1FEC/0x1FEE`。

### 6.2 快照固定帧复现（AUTOLOAD）

用户交互路径（存档选择/选项/开场/飞船）**无人值守 demo 不经过**，且失配帧号随用户操作漂移。可靠做法：把 bug 快照复制为 `saves/save0.sav`，临时在 main.c 加 `RtlSaveLoad(kSaveLoad_Load=2, 0)` 自动加载，把失配点钉死在固定帧，监控窗口即可精确命中。

### 6.3 诊断通道可靠性

- 前台 pwsh `Start-Process` 直启的 sm.exe 会在 1-2 分钟内被环境静默收割 → 长跑必须由**后台 job 挂住**实例；
- `cmd /c "sm.exe > trace.txt 2>&1"` 的文件重定向**时灵时不灵**（相同命令有时 0 字节）→ 可靠的诊断通道是游戏自写的 **`saves/sm-diag.txt`**（`extern FILE *g_diag_file`，main.c 启动时 `"w"` 打开 + `_IONBF` 无缓冲，杀进程不丢）；
- 含中文路径的 cmd 批处理文件若保存为 UTF-8，cmd（GBK）会解码成乱码——批处理里避免中文路径。

### 6.4 监控插桩要点

- DP 写（`STA $CE` 类）走 **bank 0x00 的 RAM 镜像分支**，只挂 7E/7F 分支会漏；
- 65816 取指监控中 `cpu->pc` 是**递增后**的值，比对立即数指令边界时要留 off-by-one；
- ROM 侧写路径监控打印当前源指针（$47/$48/$49）与目标指针（$4C/$4D/$4E）能直接区分"读错源"与"写错目标"两类 bug。

### 6.5 OAM 失配的定位方法论（Bug 7 沉淀）

- **失配地址 → 条目解码**：OAM 区 = RAM $0370-056F，条目 n 的 X/Y/tile/attr = $0370+4n 起；diag 的 `0x3B5` 类地址 mod 4 余 1 = **Y 字节**；
- **"mine 恒值"判读**：mine=0xFC 且 X/tile 一致 → 是**主动写入**（帧前状态为 theirs 上帧值，未写会残留它）——0xFC = 输入 y=0 + spritemap 偏移 0xFC(-4)，指向"检查值与绘制值分离"的变量漏接；
- **分层条件打印**（插桩成本低、命中率高）：先在所有 ycoord 赋值点挂 `(uint8)Y==0xFC` 条件打印（T1-T7）锁定写入函数 → 在入口挂 `y==0` 打印（TB/TP）锁定调用方 → 在计算函数挂输入输出打印（CM7/P2）锁定变量；全部走 DIAGF 双写 sm-diag.txt；
- **旋转边界类 bug 的复现**：快照保存的是**帧前 theirs 状态**（失配帧开始前）——AUTOLOAD 加载后第一帧即复现，无需等原帧号；
- **"原版为什么没有"的验证套路**：先逐字节对比两 ROM 相关 bank（本次 bank 93 实测 0 diff），确认 ROM 侧一致 → 失配只可能来自 C 移植层或触发时机，再据快照分析触发窗口（本次 = ceres_status 置位瞬间 + 槽 1 光束存活，仅 1-2 帧）。

---

## 11. 变更文件清单

| 文件 | 改动 |
|---|---|
| `src/sm_82.c` | OptionsMenuFunc4：两处 `OptionsMenuFunc5(..., 0x2C8, 0x18)` → `0x32` + 注释（Bug 1）；CHS_MapScreen_* 六个函数（Bug 5a） |
| `src/sm_8b.c` | Func20：补 4 条 DP 副作用写入 + 注释（Bug 2）；Func54 / BlackoutFromCeres：`DecompressToMem(0xE78080, ...)` → `0xE78000` + 注释（Bug 3） |
| `src/sm_93.c` | DrawBombAndProjectileExplosions：接回 CalcExplosion_Mode7 返回值（Bug 4）；DrawPlayerExplosions2：`v3 = r18 = pt.y`（Bug 7） |
| `src/sm_cpu_infra.c` | stub 重定位 F8B6 + 消息框补丁重对齐 + E3 内部补丁（Bug 5b/6） |
| `src/sm_rtl.c` | LoadSnesState 重定向常量 F8BD（Bug 5b）；栈返回平移垫片移除（25f1136） |
| `src/sm_85.c` | 消息框六处转录副作用镜像（Bug 6） |
| `saves/` | 旧 stub 时代 15 份 bug-*.sav 作废删除（25f1136）；新快照 bug-1786784933/1786796724/1786850061/1786850077.sav 保留 |

全部调试插桩（sm_rtl.c、sm_81.c、sm_90.c、sm_94.c、sm_8b.c、sm_93.c 的临时打印）已移除，工作树与 HEAD 一致。
