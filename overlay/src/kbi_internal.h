#ifndef KBI_INTERNAL_H
#define KBI_INTERNAL_H

/* ============================================================
 * keyboard_game_interface 拆分模組的內部共享頭（僅 overlay 內部使用）
 * 來源：v7.6.20 單文件版機械拆分（backup/src_split_v7620/）
 * 鐵律：地址常量/日誌 tag/OvHook_* 符號名與拆分前逐字節一致
 * ============================================================ */
#include <nds/ndstypes.h>
#include <string.h>
#include "nitro/heap.h"
#include "nitro/fs.h"
#include "nitro/pad.h"
#include "nitro/tp.h"
#include "nitro/font.h"
#include "hook.h"
#include "keyboard.h"
#include "game_variant.h"  /* 黑白雙版地址變體（GAME_WHITE 開關，common.mk 注入）*/

/* ---- no$gba / melonDS TTY 調試輸出（kbi_tty.c）---- */
void TtyLog(const char *tag, u32 a, u32 b);

/* ---- 編譯期開關 ---- */
#define STAT_SCAN_ENABLED 0
#define V7_NATIVE_ZHONG 1

#define FND_HEAP_MAGIC  0x45585048u  /* 'HPXE' (EXPH, LE u32) */

/* ---- BW 命名界面上下文佈局（kbi_input.c 定義、多處引用）---- */
#define NAMING_BUF_OFFSET    0xCC
#define NAMING_LEN_OFFSET    0xEA
#define NAMING_CAP_OFFSET    0xEE
#define NAMING_MAX_SLOTS     16

/* ---- 模式欄 BG 布局常量（kbi_modebar.c / kbi_rlog.c 引用）---- */
#define MB_MAP_BASE   0x06002000u
/* v7.6.14：欄B（漢字屏）模式欄真身在 BG1/SBB2（探測版定位，見 UpdateBarB）*/
#define MB_MAP_BASE_B 0x06001000u
#define MB_TIL_BASE   0x06004000u
#define MB_ROW        20          /* 模式欄頂行（佔 20/21/22 三行）*/
#define MB_CELL6_TX   13          /* 新格 tx13-14（x104-120）*/
#define MB_ZH_TILE_L  0x300
#define MB_ZH_TILE_R  0x301
#define MB_ZH_TOPL    0x302       /* 頂行（確認版清理過的 03C/03D 拷貝）*/
#define MB_ZH_TOPR    0x303
#define MB_ZH_BOTL    0x304       /* 底行（04C / 清理過的 04D 拷貝）*/
#define MB_ZH_BOTR    0x305

/* ---- 跨模組共享全局變量 ---- */
extern vu8 gBlockModeTouch;      /* kbi_touch.c 定義；UpdateModeBarIcon 維護 */
extern vu8 gSuppressIconTouch;   /* kbi_touch.c 定義 */
extern vu32 *gKbCur;             /* kbi_ovhook.c 定義（鉤子 asm 取址）*/
extern volatile u32 gZhongMailbox; /* kbi_ovhook.c 定義（_patch_ov194.py 郵箱臂取址）*/
extern vu32 gCurFC;              /* kbi_rlog.c 定義；FlashRenderLog 每幀更新（v7.6.24 FC 閘門）*/
extern bool gIconTriggered;      /* kbi_poll.c 定義 */
extern bool gTouchWasDown;
extern int  gTouchLastX, gTouchLastY;
extern void (*Orig_TPGetCalibratedPoint)(TPData *disp, const TPData *raw); /* kbi_touch.c */
extern u32 gShadowAddr;          /* kbi_diag.c 定義 */
extern bool gBgcLogged;
extern s16 gBrPrevX;             /* kbi_cursor.c 定義 */
extern u8  gBrOnBS;
extern u8  gBrZhongMode;
extern u8  gBrMiss;
extern u16 gBrLastX9;
extern u16 gSnapLen;
extern u32 gSnapHeap;
extern u8  gRestoreArmed;
extern bool gFontLoaded;         /* kbi_font.c 定義 */
extern NitroFontInfoSection *gFont;

/* ---- 跨模組函數 ---- */
bool LoadFont(void);
void ConvertGlyph(const u8 *glyphCell, u8 *output,
                  s32 cellWidth, s32 cellHeight,
                  const NitroGlyphMetrics* metrics);
void ReleaseFont(void);
void OnOverlayLoaded(void);
u32 FindNamingHeap(void);
void UpdateBarB(void);
void UpdateModeBarIcon(void);
void SyncRamMaps(void);
void OamDumpAll(void);
void LogBgc(void);
void SnapNaming(void);
void RestoreNamingSnap(void);
void ApplyCursorRemap(void);
bool ShouldShowKeyboard(void);
int  GetMaxInputLength(void);
void OnInputFinished(u16 *inputText, int length, bool isCanceled);
#if STAT_SCAN_ENABLED
void StatCollectChunk(void);
void StatSample(int vert);
#endif

#endif /* KBI_INTERNAL_H */
