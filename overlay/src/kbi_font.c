#include "kbi_internal.h"
#include "game_variant.h"

/* ============================================================
 * 遊戲字庫（NFTR）加載與字模轉換
 * ============================================================ */
#define FONT_BUFFER_SIZE  (256 * 1024)

static u32 gFontHeapHandle;  /* 前置聲明（GetGameHeapBuffer 寫入、定義在下方全局變量區）*/

/* 遊戲的 NITRO-System Fnd 堆分配器（arm9 靜態代碼，黑白地址見 game_variant.h）*/
typedef void *(*FndAllocFn)(void *heap, u32 size, s32 align);
typedef void  (*FndFreeFn)(void *heap, void *ptr);
#define GAME_FND_ALLOC ((FndAllocFn)ADDR_FND_ALLOC)  /* FndAllocFromExpHeapEx */
#define GAME_FND_FREE  ((FndFreeFn)ADDR_FND_FREE)    /* FndFreeToExpHeap(heap, ptr) */

#define FND_HEAP_MAGIC  0x45585048u  /* 'HPXE' (EXPH, LE u32) */


/* 遍歷遊戲堆描述符數組，找第一個能容納 size 的堆並分配 */
static void *GetGameHeapBuffer(u32 size) {
    u32 descBase = *(u32 *)ADDR_HEAP_DESC_BASE;
    if (descBase < 0x02000000 || descBase > 0x02400000) return NULL;
    u32 *desc = (u32 *)descBase;
    for (int i = 0; i < 8; i++) {
        void *heap = (void *)desc[i * 4];
        if ((u32)heap < 0x02000000 || (u32)heap > 0x02400000) continue;
        if (*(u32 *)heap != FND_HEAP_MAGIC) continue;
        void *p = GAME_FND_ALLOC(heap, size, 4);
        if (p) {
            TtyLog("HEAP", i, (u32)heap);
            gFontHeapHandle = (u32)heap;
            return p;
        }
    }
    return NULL;
}

static u8 *gFontBuffer = NULL;
NitroFontInfoSection *gFont = NULL;
bool gFontLoaded = false;
static u32 gFontHeapHandle = 0;  /* 分配字庫的遊戲堆句柄（釋放用）*/

/* 釋放字庫緩衝：鍵盤關閉後調用，把 256KB 還給遊戲堆，
 * 否則遊戲下一場景大分配失敗 → 黑屏卡死 */
void ReleaseFont(void) {
    if (!gFontLoaded || !gFontBuffer) return;
    if (gFontHeapHandle >= 0x02000000 && gFontHeapHandle <= 0x02400000 &&
        *(u32 *)gFontHeapHandle == FND_HEAP_MAGIC) {
        GAME_FND_FREE((void *)gFontHeapHandle, gFontBuffer);
        TtyLog("FREL", gFontHeapHandle, (u32)gFontBuffer);
    }
    gFontBuffer = NULL;
    gFont = NULL;
    gFontLoaded = false;
    gFontHeapHandle = 0;
}

/* CMAP/CWDH 的 nextSection 字段存的是 NFTR 相對文件偏移（指向下一 chunk 的 data，
 * 即目標地址-8 處為 chunk magic）。LoadGlyphData 把它當絕對指針用，
 * 必須在此轉換；無法通過 magic 校驗的置 NULL（鏈終止，走默認值）。 */
#define NFTR_MAGIC_PAMC 0x434D4150u  /* "PAMC" (CMAP) */
#define NFTR_MAGIC_HDWC 0x43574448u  /* "HDWC" (CWDH) */

static void FixupNftrPointers(u8 *nftrBase, u32 nftrSize) {
    /* NFTR 頭 16 字節，FINF chunk 從 nftrBase+0x10 開始 */
    u8 *finf = nftrBase + 0x10;
    NitroFontInfoSection *info = (NitroFontInfoSection *)(finf + 8);
    /* FINF 中的 chunk offset 相對於 NFTR 開始，指向 chunk 的 data 部分（跳過 magic+size）*/
    u32 cglpOff = *(u32 *)(finf + 0x10);
    u32 cwdhOff = *(u32 *)(finf + 0x14);
    u32 cmapOff = *(u32 *)(finf + 0x18);
    info->glyphSection = (NitroFontGlyphSection *)(nftrBase + cglpOff);
    info->metricsSection = (NitroFontMetricsSection *)(nftrBase + cwdhOff);
    info->charMappingSection = (NitroFontCharMappingSection *)(nftrBase + cmapOff);

    /* 修正 CWDH 鏈 */
    NitroFontMetricsSection *ms = info->metricsSection;
    while (ms) {
        u32 raw = (u32)ms->nextSection;
        if (raw == 0 || raw < 8 || raw >= nftrSize ||
            *(u32 *)(nftrBase + raw - 8) != NFTR_MAGIC_HDWC) {
            ms->nextSection = NULL;
            break;
        }
        ms->nextSection = (NitroFontMetricsSection *)(nftrBase + raw);
        ms = ms->nextSection;
    }
    /* 修正 CMAP 鏈 */
    NitroFontCharMappingSection *cm = info->charMappingSection;
    while (cm) {
        u32 raw = (u32)cm->nextSection;
        if (raw == 0 || raw < 8 || raw >= nftrSize ||
            *(u32 *)(nftrBase + raw - 8) != NFTR_MAGIC_PAMC) {
            cm->nextSection = NULL;
            break;
        }
        cm->nextSection = (NitroFontCharMappingSection *)(nftrBase + raw);
        cm = cm->nextSection;
    }
    gFont = info;
}

bool LoadFont() {
    if (gFontLoaded) return true;

    /* 從遊戲 Fnd 堆分配 256KB（遍歷描述符找有空間的堆）*/
    gFontBuffer = GetGameHeapBuffer(FONT_BUFFER_SIZE);
    if (!gFontBuffer) { TtyLog("F-ALLOC", 0, 0); return false; }

    FSFile file;
    FS_InitFile(&file);
    if (!FS_OpenFile(&file, "a/0/2/3")) { TtyLog("F-OPEN", 0, 0); return false; }
    u32 fileSize = FS_GetLength(&file);
    if (fileSize > FONT_BUFFER_SIZE) fileSize = FONT_BUFFER_SIZE;
    if (FS_ReadFile(&file, gFontBuffer, fileSize) != (s32)fileSize) {
        FS_CloseFile(&file);
        TtyLog("F-READ", fileSize, 0);
        return false;
    }
    FS_CloseFile(&file);

    u32 nftrOff = 0;
    bool found = false;
    for (u32 i = 0; i + 4 <= fileSize; i++) {
        if (gFontBuffer[i]=='R' && gFontBuffer[i+1]=='T' &&
            gFontBuffer[i+2]=='F' && gFontBuffer[i+3]=='N') {
            nftrOff = i; found = true; break;
        }
    }
    if (!found) { TtyLog("F-NFTR", 0, fileSize); return false; }

    /* NFTR 大小 = 從 nftrOff 到緩衝末尾（單個 NFTR 的偏移邊界足夠安全）*/
    FixupNftrPointers(gFontBuffer + nftrOff, fileSize - nftrOff);
    gFontLoaded = true;

    /* 調試輸出：字庫解析結果 */
    TtyLog("F-OK", nftrOff, fileSize);
    TtyLog("GCW", (u32)gFont->glyphSection,
           gFont->glyphSection->cellWidth | (gFont->glyphSection->cellHeight << 8) |
           (gFont->glyphSection->bpp << 16) | (gFont->glyphSection->cellSize << 24));
    TtyLog("CWDH", (u32)gFont->metricsSection,
           (u32)gFont->metricsSection->nextSection);
    TtyLog("CMAP", (u32)gFont->charMappingSection,
           gFont->charMappingSection->charBegin | (gFont->charMappingSection->charEnd << 16));
    TtyLog("CNEXT", (u32)gFont->charMappingSection->nextSection,
           (u32)gFont->charMappingSection->mappingInfo[0]);
    return true;
}

/* ConvertGlyph: 把 NFTR 2bpp 字模轉成鍵盤 UI 期望的 16x16 格式。
 * 輸入: glyphCell = 2bpp 字模（cellWidth 像素寬，MSB-first，1=字母 2=陰影）
 * 輸出: output = 16 行 × u32，顯示像素 k = u32 的 bit-pair k（bit 2k = 低位），
 *       像素值 0/1（UI 調色板只有 {bg,text,bg,text}，陰影值 2 會污染同 tile
 *       配對字模的調色板索引）。
 * 數據 row 0 是字庫生成工具的陰影環繞殘留（所有字模 r0 相同），跳過不用。
 */
void ConvertGlyph(const u8 *glyphCell, u8 *output,
                         s32 cellWidth, s32 cellHeight,
                         const NitroGlyphMetrics* metrics) {
    int outLeft = (16 - cellWidth) / 2;  /* 水平居中偏移（像素）*/
    if (outLeft < 0) outLeft = 0;
    int inBytesPerRow = (cellWidth * 2 + 7) / 8;
    const u8 *src = glyphCell + inBytesPerRow;  /* 跳過數據 row 0 */

    for (int row = 0; row < cellHeight; row++) {
        const u8 *inRow = src + row * inBytesPerRow;
        u32 out = 0;
        for (int px = 0; px < cellWidth; px++) {
            int bp = px * 2;
            u8 v = (inRow[bp / 8] >> (6 - bp % 8)) & 3;  /* MSB-first 2bpp */
            if (v == 1) {  /* 1=字母顯示；2=陰影不顯示 */
                int op = outLeft + px;
                if (op < 16) out |= 1u << (op * 2);
            }
        }
        *(u32 *)(output + row * 4) = out;  /* bit-pair k = 顯示像素 k */
    }
}

