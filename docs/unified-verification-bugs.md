# 统一验证记录：交互游玩暴露的三处 CHS 移植缺陷与修复

> 提交：`185df46`（src/sm_82.c、src/sm_8b.c，共 14 行插入、5 行删除）
> 验证日期：2026-08-15
> 结论：用户手动完整流程（标题 → 存档选择 → slot 1 → 选项菜单 → 开场文字 → Ceres 飞船场景）最终回归 **零失配**（sm-diag.txt 0 字节、无 bug 快照）。

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

## 5. 验证方式与结果

| 验证项 | 方法 | 结果 |
|---|---|---|
| Bug 3 修复 | bug 快照复制为 `saves/save0.sav`，临时在 main.c 帧 200 自动 `RtlSaveLoad(2,0)` 加载，30 秒内复现原失配帧 | 修复前必失配；修复后 **帧 200 零失配** |
| Bug 1/2 修复 | 同一快照场景（状态 = 选项菜单→开场→飞船前）逐帧对比 | 零失配 |
| 启动回归 | 60 秒无人值守 boot（标题 + 开场） | sm-diag.txt 0 字节 |
| 最终手动回归 | 用户完整流程：标题 → 存档选择 → slot 1 → 选项菜单 → 开场文字 → 飞船场景 | **零失配**（0 字节 diag、0 个 bug 快照） |

---

## 6. 本次调查沉淀的可复用工具与陷阱

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

---

## 7. 变更文件清单

| 文件 | 改动 |
|---|---|
| `src/sm_82.c` | OptionsMenuFunc4：两处 `OptionsMenuFunc5(..., 0x2C8, 0x18)` → `0x32` + 注释 |
| `src/sm_8b.c` | Func20：补 4 条 DP 副作用写入 + 注释 |
| `src/sm_8b.c` | Func54 / BlackoutFromCeres：`DecompressToMem(0xE78080, ...)` → `0xE78000` + 注释 |

全部调试插桩（main.c、snes.c、sm_80.c、sm_cpu_infra.c）已移除，工作树与 HEAD 一致。
