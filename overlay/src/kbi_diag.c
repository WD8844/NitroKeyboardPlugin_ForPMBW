#include "kbi_internal.h"

/* ============================================================
 * 光標定位自診斷 v4.7：OAM 逐沿傾瀉 + VRAM 128KB 差分
 * v4.6 實測（本次為真 v4.6 ROM，OAMC 已出）：
 *  - OAM 讀取有效：兩引擎各 128 條「非禁用」，帶內僅 idx 0-3/E-F/10-11
 *    共 8+8 條 y=192,x=0（隱藏到屏外的約定），隨後計數不再變化
 *  - 無 [VRA] → 括號不在 0x06000000-0x0600FFFF（SBB0-31+CBB0-3）
 * v4.7 修正兩個盲區：
 *  A) OAMC 只在計數變化時傾瀉——括號精靈若「從 y=192 隱藏位移入
 *     屏幕」（計數不變）永遠看不到。改為每次按鍵沿傾瀉帶內
 *     (y∈[96,215]) 非禁用條目，跳過 y=192&&x==0 隱藏約定，限16/引擎。
 *  B) VRAM 只掃了 64KB——CBB4-15（0x06010000+）未覆蓋。
 *     擴大到 0x06000000-0x0601FFFF（128KB，64K u16 讀/沿，仍無掉幀）。
 *  C) 首次按鍵記錄 DISPCNT+BG0-3xCNT [BGC]，核實真實 SBB/CBB 映射。
 * ============================================================ */
/* VramDiff 已移除：v4.4-v4.8 四版零輸出，BG 路線蓋棺（括號 = OAM 精靈）。
 * 移除 64KB 快照同時把 BSS 端拉回安全線內（< 0x022360BC 鐵律）。 */
bool gBgcLogged;

u32 gShadowAddr;   /* OAM 影子緩衝（0 = 未找到） */

static bool ShadowMatch(u32 sa) {
    const u32 hb = 0x07000000;
    int match = 0;
    for (u32 i = 9; i <= 12; i++) {
        if (*(vu16 *)(hb + i * 8)     == *(vu16 *)(sa + i * 8) &&
            *(vu16 *)(hb + i * 8 + 2) == *(vu16 *)(sa + i * 8 + 2) &&
            *(vu16 *)(hb + i * 8 + 4) == *(vu16 *)(sa + i * 8 + 4))
            match++;
    }
    return match >= 3;
}

static void FindOamShadow(void) {
    if (gShadowAddr) return;
    for (u32 sa = 0x02100000; sa + 1024 <= 0x02400000; sa += 8) {
        if (ShadowMatch(sa)) {
            gShadowAddr = sa;
            TtyLog("SHDW", sa, *(vu16 *)(sa + 9 * 8));
            return;
        }
    }
    gShadowAddr = 0xFFFFFFFF;   /* 標記已掃過未找到，避免重複 3MB 掃描 */
    TtyLog("SHDW", 0, 0);
}

void OamDumpAll(void) {
    static const u32 bases[2] = { 0x07000000, 0x07000400 };
    bool bracketSeen = false;
    for (int e = 0; e < 2; e++) {
        int logged = 0;
        for (u32 i = 0; i < 128 && logged < 16; i++) {
            u16 a0 = *(vu16 *)(bases[e] + i * 8);
            if ((a0 & 0x0300) == 0x0200) continue;   /* 禁用 */
            u16 a1 = *(vu16 *)(bases[e] + i * 8 + 2);
            u16 a2 = *(vu16 *)(bases[e] + i * 8 + 4);
            u32 y = a0 & 0xFF, x = a1 & 0x1FF;
            if (y == 192 && x == 0) continue;        /* 屏外隱藏約定 */
            if (y >= 96 && y <= 215) {
                /* v4.8：記錄完整 a0/a1/a2（形狀/尺寸/翻轉必須保留）
                 * arg1 = e | idx<<8 | a0<<16；arg2 = a1 | a2<<16 */
                TtyLog(e ? "OAMS" : "OAMM",
                       (u32)e | (i << 8) | ((u32)a0 << 16),
                       (u32)a1 | ((u32)a2 << 16));
                logged++;
                if (y >= 150 && y <= 190) bracketSeen = true;
            }
        }
    }
    if (bracketSeen) FindOamShadow();
}

void LogBgc(void) {
    /* BG0-3xCNT（0x04000008-E，含 SBB/CBB 位）+ 兩引擎 DISPCNT */
    u16 bg0 = *(vu16 *)0x04000008, bg1 = *(vu16 *)0x0400000A;
    u16 bg2 = *(vu16 *)0x0400000C, bg3 = *(vu16 *)0x0400000E;
    u16 da = *(vu16 *)0x04000000, db = *(vu16 *)0x04001000;
    TtyLog("BGCA", (u32)bg0 | ((u32)bg1 << 16), (u32)bg2 | ((u32)bg3 << 16));
    TtyLog("BGCB", (u32)da | ((u32)db << 16), 0);
}
