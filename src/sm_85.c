// Message boxes
#include "ida_types.h"
#include "variables.h"
#include "funcs.h"

uint16 message_box_das0l_value;


#define kMessageBoxDefs ((MsgBoxConfig*)RomFixedPtr(0x85869b))
static const uint16 kMsgBoxSpecialButtonTilemapOffs[27] = {  // 0x8583D1
  0, 0x12a, 0x12a, 0x12c, 0x12c, 0x12c, 0, 0, 0, 0, 0, 0, 0x120, 0, 0, 0,
  0,     0, 0x12a,     0,     0,     0, 0, 0, 0, 0, 0,
};
static const uint16 kTileNumbersForButtonLetters[8] = { 0x28e0, 0x3ce1, 0x2cf7, 0x38f8, 0x38d0, 0x38eb, 0x38f1, 0x284e };

// ===== CHS 补丁新增代码：消息框引擎（bank E3:8527-87DA）=====
// 来源：CHS 把 bank 85 的消息框函数改为 JSL/JML 到 bank E3 的新增代码。
// 纯延时循环（$E3:8767）与音乐/SFX 调用（JSL $808F0C/$8289EF）按 C 惯例省略。

static void MsgBox_CHS_880D(void) {  // $E3:880D（E38527 的子函数）
  // 消息框类型 0x14-0x1D 直接返回（E3886F: SEP #$20/BRA→RTS）；否则
  // DMA E6:A5F7 0x40 字节到 VRAM（$2116=$34-0x20）。
  uint16 mi = message_box_index;
  if (mi == 0x14 || mi == 0x15 || mi == 0x16 || mi == 0x17 || mi == 0x18
      || mi == 0x1B || mi == 0x1C || mi == 0x1D)
    return;
  WriteRegWord(VMADDL, *(uint16 *)&g_ram[0x34] - 0x20);  // LDA $34/SBC #$20/STA $2116
  WriteRegWord(DMAP1, 0x1801);
  WriteRegWord(A1T1L, 0xA5F7);
  WriteReg(A1B1, 0xE6);
  WriteRegWord(DAS1L, 0x40);
  WriteReg(0x4317, 0);  // STZ $4317
  WriteReg(0x4319, 0);  // STZ $4319
  WriteReg(VMAIN, 0x80);
  WriteReg(MDMAEN, 2);
}

static void MsgBox_CHS_853A(void) {  // $E3:853A（E38527 的子函数）
  // 按 message_box_index 从 E6A2B0 表（3 字节项 {lo,hi,bank}）做 4 段 VRAM DMA。
  if (!message_box_index)  // LDA $7E1C1F/BEQ（RTS）
    return;
  uint16 t = message_box_index - 1;                    // DEC
  uint16 x = (uint16)(t + (uint16)(t << 1));           // ASL/CLC/ADC $CC（×3 16 位截断）
  const uint8 *b = RomFixedPtr(0xE6A2B0 + x);
  uint16 src = b[0] | (b[1] << 8);
  // 段1：+0x80、0x60 字节、VRAM $42C0、bank E6A2B2[X]
  WriteRegWord(A1T1L, src + 0x80);
  WriteRegWord(DAS1L, 0x60);
  WriteRegWord(VMADDL, 0x42C0);
  WriteReg(A1B1, b[2]);
  WriteReg(MDMAEN, 2);
  // 段2：+0x200、0x60、VRAM $4380
  WriteRegWord(A1T1L, src + 0x200);
  WriteRegWord(DAS1L, 0x60);
  WriteRegWord(VMADDL, 0x4380);
  WriteReg(A1B1, b[2]);
  WriteReg(MDMAEN, 2);
  // 段3：+0x280、0x80、VRAM $43C0
  WriteRegWord(A1T1L, src + 0x280);
  WriteRegWord(DAS1L, 0x80);
  WriteRegWord(VMADDL, 0x43C0);
  WriteReg(A1B1, b[2]);
  WriteReg(MDMAEN, 2);
  // 段4：E6A307[X] 16 位源、0x190 字节、VRAM $4580（bank $4314 残留 E6 未写）
  const uint8 *b2 = RomFixedPtr(0xE6A307 + x);
  WriteRegWord(A1T1L, b2[0] | (b2[1] << 8));
  WriteRegWord(DAS1L, 0x190);
  WriteRegWord(VMADDL, 0x4580);
  WriteReg(MDMAEN, 2);
}

static void MsgBox_CHS_8615(void) {  // $E3:8615（E38527 的子函数）
  // 消息框类型 2-6/0D/13 → 按 $C8 从 E6A3B5/E6A3CD 表做 4 段 DMA（X=(Y-1)*8，段间 +2）。
  uint16 mi = message_box_index;
  if (mi != 2 && mi != 3 && mi != 4 && mi != 5 && mi != 6 && mi != 0xD && mi != 0x13)
    return;  // JMP $8704（PLY/PLX/RTS）
  uint16 x = (uint16)((*(uint16 *)&g_ram[0xC8] - 1) * 8);  // LDY $C8/DEC/ASL/ASL/ASL
  for (int i = 0; i < 4; i++) {
    uint16 xi = (uint16)(x + i * 2);                        // 段间 INX INX
    const uint8 *t = RomFixedPtr(0xE6A3B5 + xi);            // {lo,hi,bank} 项
    WriteRegWord(DAS1L, 0x10);
    WriteRegWord(VMADDL, *(uint16 *)RomFixedPtr(0xE6A3CD + xi));  // LDA $E6A3CD,X
    WriteRegWord(A1T1L, (uint16)(*(uint16 *)t + i * 0x10));       // LDA $E6A3B5,X + ADC #$10i
    WriteReg(A1B1, t[2]);                                   // LDA $E6A3B7,X
    WriteReg(MDMAEN, 2);
  }
}

static void MsgBox_CHS_8527(void) {  // $E3:8527（$85:835B JSL 目标）
  // JSR $8767（延时）与 JSL $808F0C（音乐）省略。
  MsgBox_CHS_880D();
  MsgBox_CHS_853A();
  MsgBox_CHS_8615();
}

static void MsgBox_CHS_8774(void) {  // $E3:8774（$85:82B8 JSL 目标）：中文消息框 tilemap 加载
  // JSR $8767 延时省略；JSL $808F0C/$8289EF 音乐/SFX 按 C 惯例省略。
  message_box_animation_y1 = 0x70;        // LDA #$70/STA $05A6
  message_box_animation_y0 = 0x7C;        // LDA #$7C/STA $05A4
  message_box_animation_y_radius = 0;     // STZ $05A2
  memset(g_ram + 0x3000, 0, 0xE0);        // LDX #$00/LDA #$00/STA $7E3000,X/INX INX/CPX #$E0/BNE
  if (message_box_index) {                // LDA $7E1C1F/BEQ
    uint16 t = message_box_index - 1;                   // DEC
    uint16 x = (uint16)(t + (uint16)(t << 1));          // ASL/CLC/ADC $CC（×3）
    const uint8 *e = RomFixedPtr(0xE6A35E + x);         // 3 字节指针表 {lo,hi,bank}
    uint32 src = e[0] | (e[1] << 8) | ((uint32)e[2] << 16);
    memcpy(g_ram + 0x3200, RomFixedPtr(src), 0x300);    // 0x180 字（CPX #$180）
  }
  // STX $09 位于比对白名单（0x00-0x50），无观察效果——忽略。
}

static void MsgBox_CHS_87DA(uint16 y) {  // $E3:87DA（$85:840C JSL 目标）：4 个边角 tile
  *(uint16 *)&g_ram[0xC8] = (uint16)(y * 3);            // STY $C8/TYA/ASL/ADC $C8/STA $C8
  uint16 t = message_box_index - 1;                     // LDA $7E1C1F/DEC
  uint16 x1 = *(uint16 *)RomFixedPtr(0xE6A4B5 + (uint16)(t << 1));  // ASL/TAX/LDA $E6A4B5,X/TAX
  uint8 v = RomFixedPtr(0xE6A4EF + (y >> 1))[0];        // TYA/LSR/TAX/SEP/LDA $E6A4EF,X
  g_ram[0x3201 + x1] = v;  // STA $7E3201,X 等 4 处（SEP #$20 后 8 位写）
  g_ram[0x3203 + x1] = v;
  g_ram[0x3241 + x1] = v;
  g_ram[0x3243 + x1] = v;
}

static void MsgBox_CHS_8707(void) {  // $E3:8707（$85:8526 JML 目标）：保存确认 tilemap 拷贝
  // 进入时 X=0x100、Y=0x80（ToggleSaveConfirmationSelection 的循环上下文）。
  // CPY #$0080/BEQ：DBR=E6，E6:A4F7 起 0x40 字 → $7E31C0+X（=$7E32C0），JML $858532。
  memcpy(g_ram + 0x31C0 + 0x100, RomFixedPtr(0xE6A4F7), 0x80);
  // CPY #$0040 分支（E6:A577）与 fallthrough（STA $7E3200,X/JML $85852A）为死代码：
  // 8526 调用点 Y 恒为 0x80（LDY #$0080），两分支不可达（asm 注释备查）。
}


static void InitializePpuForMessageBoxes(void) {  // 0x858143
  save_confirmation_selection = 0;
  WriteReg(HDMAEN, 0);
  WriteReg(CGADD, 0x19);
  WriteReg(CGDATA, 0xB1);
  WriteReg(CGDATA, 0xB);
  WriteReg(CGDATA, 0x1F);
  WriteReg(CGDATA, 0);
  ram3000.msgbox.backup_of_enabled_hdma_channels = reg_HDMAEN;
  ram3000.misc.field_3E8[3] = gameplay_BG3SC;
  gameplay_BG3SC = 88;
  gameplay_TM = 23;
  gameplay_CGWSEL = 0;
  gameplay_CGADSUB = 0;
  WriteReg(COLDATA, 0x20);
  WriteReg(COLDATA, 0x40);
  WriteReg(COLDATA, 0x80);
  ReadReg(BG3HOFS);
  WriteReg(BG3HOFS, 0);
  WriteReg(BG3HOFS, 0);
  ReadReg(BG3VOFS);
  WriteReg(BG3VOFS, 0);
  WriteReg(BG3VOFS, 0);
  for (int i = 128; i >= 0; i -= 2)
    *(uint16 *)((uint8 *)ram3000.pause_menu_map_tilemap + (uint16)i) = 0;
  WriteRegWord(VMADDL, addr_unk_605880);
  ReadRegWord(RDVRAML);
  WriteRegWord(DMAP1, 0x3981);
  WriteRegWord(A1T1L, ADDR16_OF_RAM(ram4000) + 256);
  WriteRegWord(A1B1, 0x7E);
  WriteRegWord(DAS1L, 0x700);
  WriteRegWord(DAS10, 0);
  WriteRegWord(A2A1H, 0);
  WriteReg(VMAIN, 0x80);
  WriteReg(MDMAEN, 2);
}

static void ClearMessageBoxBg3Tilemap(void) {  // 0x8581F3
  int16 v0;
  v0 = 1790;
  do {
    *(uint16 *)((uint8 *)ram3800.cinematic_bg_tilemap + (uint16)v0) = 0xe;
    v0 -= 2;
  } while (v0 >= 0);
  WriteRegWord(VMADDL, addr_unk_605880);
  WriteRegWord(DMAP1, 0x1801);
  WriteRegWord(A1T1L, ADDR16_OF_RAM(ram3800));
  WriteRegWord(A1B1, 0x7E);
  WriteRegWord(DAS1L, 0x700);
  WriteRegWord(DAS10, 0);
  WriteRegWord(A2A1H, 0);
  WriteReg(VMAIN, 0x80);
  WriteReg(MDMAEN, 2);
}

static const uint16 kLargeMsgBoxTopBottomBorderTilemap[32] = {  // 0x85825A
     0xe,    0xe,    0xe, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e, 0x280f, 0x280f, 0x280f, 0x280f, 0x280f, 0x280f, 0x280f,
  0x280f, 0x280f, 0x280f, 0x280f, 0x280f, 0x280f, 0x280f, 0x280f, 0x280f, 0x280f, 0x280f, 0x284e, 0x284e,    0xe,    0xe,    0xe,
};
static const uint16 kSmallMsgBoxTopBottomBorderTilemap[32] = {
     0xe,    0xe,    0xe,    0xe,    0xe,    0xe, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e,
  0x284e, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e, 0x284e,    0xe,    0xe,    0xe,    0xe,    0xe,    0xe,    0xe,
};

static uint16 WriteMessageTilemap(void) {  // 0x8582B8
  // CHS: JSL $808F0C / JSL $8289EF / JSL $E38774 / JSR $8136 / RTS
  // （音乐/SFX/8136 按 C 惯例省略；原版初始化与 kMessageBoxDefs 拷贝被 E38774 替代）
  MsgBox_CHS_8774();
  return 0;  // 调用方（WriteLarge/WriteSmall 已简化）不再使用返回值
}

static void WriteLargeMessageBoxTilemap(void) {
  // CHS: 85825A LDX #$0000 → JSR $82B8/RTS（头尾 32 字边框拷贝循环删除）
  // kLargeMsgBoxTopBottomBorderTilemap 保留定义：ROM $858000 区 CHS 未改，供对照。
  WriteMessageTilemap();
}

static void WriteSmallMessageBoxTilemap(void) {  // 0x858289
  // CHS: 858289 LDY/LDX #$0000 → JSR $82B8/RTS（头尾 32 字边框拷贝循环删除）
  WriteMessageTilemap();
}

static void CallMsgBoxDraw(uint32 ea) {
  switch (ea) {
  case fnWriteLargeMessageBoxTilemap: WriteLargeMessageBoxTilemap(); return;
  case fnWriteSmallMessageBoxTilemap: WriteSmallMessageBoxTilemap(); return;
  default: Unreachable();
  }
}

static void MsgBoxMakeHdmaTable(void) {
  message_box_animation_y2 = (uint16)(31488 - message_box_animation_y_radius) >> 8;
  message_box_animation_y3 = 99;
  message_box_animation_y0 = (uint16)(message_box_animation_y_radius + 31744) >> 8;
  message_box_animation_y1 = 148;
  uint16 v0 = 123;
  uint16 v1 = 124;
  uint16 r20 = 30;
  do {
    ram3000.pause_menu_map_tilemap[v0] = message_box_animation_y3 - message_box_animation_y2;
    --message_box_animation_y3;
    --message_box_animation_y2;
    ram3000.pause_menu_map_tilemap[v1] = message_box_animation_y1 - message_box_animation_y0;
    --v0;
    ++message_box_animation_y1;
    ++message_box_animation_y0;
    ++v1;
  } while (--r20);
  uint16 v2 = v1 * 2;
  do {
    *(uint16 *)((uint8 *)ram3000.pause_menu_map_tilemap + v2) = 0;
    v2 += 2;
  } while ((int16)(v2 - 480) < 0);
}

static void SetupMessageBoxBg3YscrollHdma(void) {  // 0x858363
  *(uint32 *)&ram3000.menu.palette_backup_in_menu[64] = -516947713;
  ram3000.pause_menu_map_tilemap[450] = 12542;
  ram3000.msgbox.indirect_hdma[6] = 0;
  WriteReg(DMAP6, 0x42);
  WriteReg(BBAD6, 0x12);
  WriteReg(A1T6L, 0x80);
  WriteReg(DAS6L, 0x80);
  WriteReg(A1T6H, 0x33);
  WriteReg(DAS6H, 0x33);
  WriteReg(A1B6, 0x7E);
  WriteReg(DAS60, 0x7E);
  WriteReg(A2A6L, 0);
  WriteReg(A2A6H, 0);
  WriteReg(NTRL6, 0);
  MsgBoxMakeHdmaTable();
  WriteReg(HDMAEN, 0x40);
}

static void SetupPpuForActiveMessageBox(uint16 r52) {  // 0x85831E
  SetupMessageBoxBg3YscrollHdma();
  r52 += 22528;
  WriteRegWord(VMADDL, r52);
  WriteRegWord(DMAP1, 0x1801);
  WriteRegWord(A1T1L, ADDR16_OF_RAM(ram3000) + 512);
  WriteRegWord(A1B1, 0x7E);
  WriteRegWord(DAS1L, message_box_das0l_value);
  WriteRegWord(DAS10, 0);
  WriteRegWord(A2A1H, 0);
  WriteReg(VMAIN, 0x80);
  WriteReg(MDMAEN, 2);
  MsgBox_CHS_8527();  // CHS: 85835A JSL $808F0C → JSL $E38527（原版音乐调用被替换为中文图形上传）
}

static void DrawSpecialButtonAndSetupPpuForLargeMessageBox(uint16 a) {
  uint16 v1 = 0;
  if ((a & kButton_A) == 0) {
    v1 = 2;
    if ((a & 0x8000) == 0) {
      v1 = 4;
      if ((a & kButton_X) == 0) {
        v1 = 6;
        if ((a & kButton_Y) == 0) {
          v1 = 8;
          if ((a & kButton_Select) == 0) {
            v1 = 10;
            if ((a & kButton_L) == 0) {
              v1 = 12;
              if ((a & kButton_R) == 0)
                v1 = 14;
            }
          }
        }
      }
    }
  }
  *(uint16 *)((uint8 *)&ram3000.pause_menu_map_tilemap[256] + kMsgBoxSpecialButtonTilemapOffs[message_box_index - 1]) = kTileNumbersForButtonLetters[v1 >> 1];
  MsgBox_CHS_87DA((a & 0x10) ? 0x0C00 : 0x0E00);  // CHS: 85840C 原版单 tile 写（LDA $1C1F/…/STA $7E3200,X）→ JSL $E387DA + JMP $841D
  SetupPpuForActiveMessageBox(416);
}

static void DrawShootButtonAndSetupPpuForLargeMessageBox(void) {  // 0x8583C5
  DrawSpecialButtonAndSetupPpuForLargeMessageBox(button_config_shoot_x);
}

static void DrawRunButtonAndSetupPpuForLargeMessageBox(void) {  // 0x8583CC
  DrawSpecialButtonAndSetupPpuForLargeMessageBox(button_config_run_b);
}

static void SetupPpuForSmallMessageBox(void) {  // 0x858436
  SetupPpuForActiveMessageBox(416);  // CHS: 858439 LDA #$01C0 → LDA #$01A0（448→416）
}

static void SetupPpuForLargeMessageBox(void) {  // 0x858441
  SetupPpuForActiveMessageBox(416);
}

static void CallMsgBoxModify(uint32 ea) {
  switch (ea) {
  case fnDrawShootButtonAndSetupPpuForLargeMessageBox: DrawShootButtonAndSetupPpuForLargeMessageBox(); return;
  case fnDrawRunButtonAndSetupPpuForLargeMessageBox: DrawRunButtonAndSetupPpuForLargeMessageBox(); return;
  case fnSetupPpuForSmallMessageBox: SetupPpuForSmallMessageBox(); return;
  case fnSetupPpuForLargeMessageBox: SetupPpuForLargeMessageBox(); return;
  default: Unreachable();
  }
}

static void InitializeMessageBox(void) {  // 0x858241
  int v0 = message_box_index - 1;
  CallMsgBoxDraw(kMessageBoxDefs[v0].draw_initial_tilemap | 0x850000);
  CallMsgBoxModify(kMessageBoxDefs[v0].modify_box_func | 0x850000);
}


static const uint16 kSaveConfirmationSelectionTilemap[96] = {  // 0x858507
     0xe,    0xe,    0xe,    0xe,    0xe,    0xe, 0x3c4e, 0x3c4e, 0x38cc, 0x38cd, 0x3cf8, 0x3ce4, 0x3cf2, 0x3c4e, 0x3c4e, 0x3c4e,
  0x3c4e, 0x3c4e, 0x3c4e, 0x2ced, 0x2cee, 0x3c4e, 0x3c4e, 0x3c4e, 0x3c4e,    0xe,    0xe,    0xe,    0xe,    0xe,    0xe,    0xe,
     0xe,    0xe,    0xe,    0xe,    0xe,    0xe, 0x3c4e, 0x3c4e, 0x38cc, 0x38cd, 0x3cf8, 0x3ce4, 0x3cf2, 0x3c4e, 0x3c4e, 0x3c4e,
  0x3c4e, 0x3c4e, 0x3c4e, 0x2ced, 0x2cee, 0x3c4e, 0x3c4e, 0x3c4e, 0x3c4e,    0xe,    0xe,    0xe,    0xe,    0xe,    0xe,    0xe,
     0xe,    0xe,    0xe,    0xe,    0xe,    0xe, 0x3c4e, 0x3c4e, 0x3c4e, 0x3c4e, 0x2cf8, 0x2ce4, 0x2cf2, 0x3c4e, 0x3c4e, 0x3c4e,
  0x3c4e, 0x38cc, 0x38cd, 0x3ced, 0x3cee, 0x3c4e, 0x3c4e, 0x3c4e, 0x3c4e,    0xe,    0xe,    0xe,    0xe,    0xe,    0xe,    0xe,
};

static void ToggleSaveConfirmationSelection(void) {
  save_confirmation_selection ^= 2;
  uint16 v0 = 64;
  if (save_confirmation_selection == 2)
    v0 = 128;
  uint16 v1 = 128;
  int r52 = 32;
  MsgBox_CHS_8707();  // CHS: 858526 STA $7E3200,X → JML $E38707（源 $9581,Y → E6:A4F7 起 0x40 字，E3 代码 JML $858532 跳回循环尾）
  (void)v0;
  (void)v1;
  (void)r52;
  WriteRegWord(VMADDL, addr_unk_6059A0);
  WriteRegWord(DMAP1, 0x1801);
  WriteRegWord(A1T1L, ADDR16_OF_RAM(ram3000) + 512);
  WriteRegWord(A1B1, 0x7E);
  WriteRegWord(DAS1L, 0x180);
  WriteRegWord(DAS10, 0);
  WriteRegWord(A2A1H, 0);
  WriteReg(VMAIN, 0x80);
  WriteReg(MDMAEN, 2);
}

static void RestorePpuForMessageBox(void) {  // 0x85861A
  WriteRegWord(VMADDL, addr_unk_605880);
  WriteRegWord(DMAP1, 0x1801);
  WriteRegWord(A1T1L, ADDR16_OF_RAM(ram4000) + 256);
  WriteRegWord(A1B1, 0x7E);
  WriteRegWord(DAS1L, 0x700);
  WriteRegWord(DAS10, 0);
  WriteRegWord(A2A1H, 0);
  WriteReg(VMAIN, 0x80);
  WriteReg(MDMAEN, 2);
  reg_HDMAEN = ram3000.msgbox.backup_of_enabled_hdma_channels;
  WriteReg(HDMAEN, ram3000.msgbox.backup_of_enabled_hdma_channels);
  gameplay_BG3SC = ram3000.misc.field_3E8[3];
  gameplay_TM = reg_TM;
  gameplay_CGWSEL = next_gameplay_CGWSEL;
  gameplay_CGADSUB = next_gameplay_CGADSUB;
  WriteReg(CGADD, 0x19);
  WriteReg(CGDATA, palette_buffer[25]);
  WriteReg(CGDATA, HIBYTE(palette_buffer[25]));
  WriteReg(CGDATA, palette_buffer[26]);
  WriteReg(CGDATA, HIBYTE(palette_buffer[26]));
}

static CoroutineRet OpenMessageBox_Async(void) {  // 0x85844C
  COROUTINE_BEGIN(coroutine_state_4, 0);
  for (message_box_animation_y_radius = 0;; message_box_animation_y_radius += 512) {
    COROUTINE_AWAIT(1, WaitForNMI_NoUpdate_Async());
    HandleMusicQueue();
    HandleSoundEffects();
    MsgBoxMakeHdmaTable();
    if (message_box_animation_y_radius == 0x1800)
      break;
  }
  COROUTINE_END(0);
}

static CoroutineRet HandleMessageBoxInteraction_Async(void) {  // 0x85846D
  COROUTINE_BEGIN(coroutine_state_4, 0);

  if (message_box_index == 23 || message_box_index == 28) {
    save_confirmation_selection = 0;
    while (1) {
      do {
        COROUTINE_AWAIT(1, WaitForNMI_NoUpdate_Async());
        HandleMusicQueue();
        HandleSoundEffects();
        ReadJoypadInputs();
      } while (!joypad1_newkeys);
      if ((joypad1_newkeys & kButton_A) != 0)
        break;
      if ((joypad1_newkeys & kButton_B) != 0) {
        save_confirmation_selection = 2;
        goto GETOUT;
      }
      if ((joypad1_newkeys & (kButton_Select | kButton_Left | kButton_Right)) != 0) {
        ToggleSaveConfirmationSelection();
        QueueSfx1_Max6(0x37);
      }
    }
  } else {
    my_counter = 10;
    if (message_box_index != 20 && message_box_index != 21 && message_box_index != 22 && message_box_index != 24)
      my_counter = 360;
    do {
      COROUTINE_AWAIT(2, WaitForNMI_NoUpdate_Async());
      HandleMusicQueue();
      HandleSoundEffects();
    } while (--my_counter);
    do {
      COROUTINE_AWAIT(3, WaitForNMI_NoUpdate_Async());
      ReadJoypadInputs();
    } while ((bug_fix_counter < 1 ? joypad1_newkeys : joypad1_lastkeys) == 0);
  }
GETOUT:;
  COROUTINE_END(0);
}

static CoroutineRet CloseMessageBox_Async(void) {  // 0x858589
  COROUTINE_BEGIN(coroutine_state_4, 0);
  do {
    COROUTINE_AWAIT(1, WaitForNMI_NoUpdate_Async());
    HandleMusicQueue();
    HandleSoundEffects();
    MsgBoxMakeHdmaTable();
    message_box_animation_y_radius -= 512;
  } while ((message_box_animation_y_radius & 0x8000) == 0);
  COROUTINE_END(0);
}

CoroutineRet DisplayMessageBox_Async(uint16 a) {  // 0x858080
  COROUTINE_BEGIN(coroutine_state_3, 0)
  message_box_index = a;
  CancelSoundEffects();
  InitializePpuForMessageBoxes();
  ClearMessageBoxBg3Tilemap();
  InitializeMessageBox();
  COROUTINE_AWAIT(2, OpenMessageBox_Async());
  COROUTINE_AWAIT(3, HandleMessageBoxInteraction_Async());
  COROUTINE_AWAIT(4, CloseMessageBox_Async());

  if (message_box_index == 28 && save_confirmation_selection != 2) {
    message_box_index = 24;
    ClearMessageBoxBg3Tilemap();
    QueueSfx1_Max6(0x2E);

    my_counter = 160;
    do {
      HandleMusicQueue();
      HandleSoundEffects();
      COROUTINE_AWAIT(8, WaitForNMI_NoUpdate_Async());
    } while (--my_counter);

    InitializeMessageBox();
    COROUTINE_AWAIT(5, OpenMessageBox_Async());
    COROUTINE_AWAIT(6, HandleMessageBoxInteraction_Async());
    COROUTINE_AWAIT(7, CloseMessageBox_Async());
    message_box_index = 28;
  }
  ClearMessageBoxBg3Tilemap();
  RestorePpuForMessageBox();
  QueueSamusMovementSfx();

  if (message_box_index == 20) {
    game_state = kGameState_12_Pausing;
  }
  COROUTINE_END(0);
  //  else if (message_box_index == 28 || message_box_index == 23) {
  //    return save_confirmation_selection;
  //  }
  //  return 0;
}

int DisplayMessageBox_Poll(uint16 a) {
  if (a == message_box_index) {
    message_box_index = 0;
    return save_confirmation_selection;
  }
  queued_message_box_index = a;
  return -1;
}

void DisplayMessageBox(uint16 a) {  // 0x858080
  queued_message_box_index = a;
}
