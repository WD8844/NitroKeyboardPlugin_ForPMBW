#include "kbi_internal.h"

/* ============================================================
 * 寶可夢黑（ver2.1.8漢化）鍵盤插件適配
 *
 * 字庫：a/0/2/3（NARC，第一個 NFTR 12×15 2bpp）
 * 編碼：UTF-16
 *
 * 字庫緩衝用遊戲自己的 Fnd 堆分配器（逆向 arm9 @0x0202FA04 getter）：
 *   [0x021466F4] = 堆描述符數組基址（0x02226400，每條16字節，u32=堆句柄）
 *   句柄指向 EXPH('HPXE') 堆結構；用 FndAllocFromExpHeapEx 分配。
 * ============================================================ */

/* ============================================================ */

static void* Alloc(u32 size) {
    static u8 heap[64 * 1024];
    return heap;
}
static void Free(void *ptr) {}

static bool LoadGlyph(u16 charCode, u8 *output, int *advance) {
    if (!gFontLoaded && !LoadFont()) { TtyLog("LG-NOFONT", charCode, 0); return false; }
    if (!gFont) return false;
    const u8 *glyphCell;
    NitroGlyphMetrics metrics;
    memset(output, 0, 64);
    if (LoadGlyphData(gFont, charCode, &glyphCell, &metrics)) {
        ConvertGlyph(glyphCell, output,
                     gFont->glyphSection->cellWidth,
                     gFont->glyphSection->cellHeight, &metrics);
        *advance = metrics.advance + 1;
        TtyLog("LG-OK", charCode, (u32)glyphCell);
        TtyLog("MET", metrics.left | (metrics.width << 8) | (metrics.advance << 16),
               (u32)glyphCell - (u32)gFont->glyphSection->glpyhData);
        return true;
    }
    TtyLog("LG-FAIL", charCode, 0);
    return false;
}

static bool KeycodeToChar(u16 keycode, u16 *output) { *output = keycode; return true; }
static bool CanContinueInput(u16 *inputText, int length, u16 nextChar) { return true; }

KeyboardGameInterface * GetKeyboardGameInterface() {
    static KeyboardGameInterface gi = {
        .Alloc = Alloc, .Free = Free, .OnOverlayLoaded = OnOverlayLoaded,
        .ShouldShowKeyboard = ShouldShowKeyboard,
        .GetMaxInputLength = GetMaxInputLength,
        .GetInitialInputText = NULL,
        .LoadGlyph = LoadGlyph, .KeycodeToChar = KeycodeToChar,
        .CanContinueInput = CanContinueInput,
        .OnInputFinished = OnInputFinished
    };
    return &gi;
}
