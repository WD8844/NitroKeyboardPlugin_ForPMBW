#include "kbi_internal.h"

/* ---- v7.6.15：寬格確認閃塊 +16px 觀測鉤子（純診斷，定位後移除）----
 * ov194 閃塊渲染器 0x021F18E4 的兩個外部 bl 站點由 _patch_ov194.py 重定向
 * 到此：0x021F30A2（mode=1 確認渲染）/ 0x021F30CE（mode=r7 分支渲染）。
 * 原始調用約定（真機反彙編實錘）：
 *   r0=obj+0x20、r1=mode、r2=rect(&sp+0x14)、r3=[obj+0xE6]、棧參=[obj+0xFC]
 * （棧參即閃塊 X 輸入：渲染器內 X_tile = FC*2+3 @0x021F1968）。
 * 本包裝按 AAPCS 收 5 參（第 5 參=棧參），轉儲 FC/col/row/arg1-arg4 後
 * 原樣轉發原渲染器並透傳返回值——渲染行為字節級不變。 */
#define RLOG_RENDERER  ADDR_OV194_RENDERER   /* 原渲染器 Thumb 入口（|1，黑白見 game_variant.h） */

/* v7.6.24：當前鍵盤佈局號（FC=[obj+0xFC]，v7.6.17 真機實錘粘滯：
 * 欄A=0、欄B=4、數字=3、QWERTY=5）快照——FlashRenderLog 每幀更新，
 * PollIconTouch 借此做 SyncRamMaps 閘門。0xFFFFFFFF=尚未見過渲染器。 */
vu32 gCurFC = 0xFFFFFFFFu;

static u32 FlashRenderLog(u32 site, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    /* v7.6.23：渲染器站點即 fill() 寫 RAM buffer 的現場——遊戲在切換鍵盤/
     * 輸入等事件後會把模式欄整條重繪回原生（本輪 MSRA×3/MBRB2×2 日誌實錘），
     * 下一輪 PollIconTouch 才補右移 = 1 幀原生閃爍（用戶實測「隨機錯位」）。
     * 此處冪等補一次 SyncRamMaps：buffer 已右移時零副作用，native 時在
     * 渲染器 fill/遊戲 memcpy 之前修正，閃爍窗口壓到 0（凡走渲染器的幀）。
     * v7.6.24：改為 FC 閘門內調用——SyncRamMaps 的原生特徵偵測（rows20-22
     * c13=01D/023/02D）無法區分欄B(fc=4) 與數字(fc=3)/QWERTY(fc=5) 屏
     * （底欄幾何+簽名全同，v7.6.23 測試日誌實錘 MSRA(1,11) 全部發生在
     * fc=3/fc=5 渲染之後、無一次 fc=4），把數字/QWERTY 屏底欄整條誤右移
     * 16px =「space 右側貼圖不對齊、右下角向右偏移、花紋不自然」。
     * 只有欄A(fc=0)/欄B(fc=4) 才補同步；FCG=佈局切換觀測（FC 變化才記，
     * 真機可驗證閘門覆蓋與否）。 */
    u32 obj = a0 - 0x20;
    u32 fc = 0xFFFFFFFFu;
    if (obj >= 0x02000000u && obj < 0x03000000u)
        fc = *(vu32 *)(obj + 0xFC);
    if (fc != gCurFC) {
        gCurFC = fc;
        TtyLog("FCG", fc, a1);
    }
    if (fc == 0 || fc == 4)
        SyncRamMaps();
    u32 col = 0xFF, row = 0xFF;
    if (obj >= 0x02000000u && obj < 0x03000000u) {
        col = *(vu8 *)(obj + 0xD4);
        row = *(vu8 *)(obj + 0xD8);
    }
    char tag[8];
    tag[0] = 'R'; tag[1] = 'L'; tag[2] = (char)('A' + site); tag[3] = 0;
    TtyLog(tag, fc | (col << 8) | (row << 16), a1);
    tag[3] = '2'; tag[4] = 0;
    TtyLog(tag, a2, (a3 & 0xFFFF) | ((a4 & 0xFFFF) << 16));
    /* v7.6.17：rect 內容轉儲（RLx3=x0|y0<<16、RLx4=x1|y1<<16）
     * 渲染器 mode==2 確認閃塊 = fill(2, x0,y0,w,h,pal3) + fill(1, 同參)。
     * ⚠️v7.6.18 修正範圍守衛：本作為 TWL 16MB 主 RAM（0x02FFFFFF），
     * 舊 0x02400000 上界把 rect 指針 0x02FE3684 排除掉（RLx3/4 從未輸出）。 */
    if (a2 >= 0x02000000u && a2 < 0x03000000u) {
        tag[3] = '3'; tag[4] = 0;
        TtyLog(tag, *(vu32 *)a2, *(vu32 *)(a2 + 4));
        tag[3] = '4'; tag[4] = 0;
        TtyLog(tag, *(vu32 *)(a2 + 8), *(vu32 *)(a2 + 0xC));
    }
    /* v7.6.17：一次性轉儲填充函數 0x02041208 運行時字節（ARM）。
     * arm9 靜態鏡像該處非代碼（運行時被遊戲解壓/覆蓋），靜態逆向不可達，
     * 只能真機轉儲。FLD00-3F：每條 2 個 u32，共 0x200B。 */
    static u8 sFillDumped;
    if (!sFillDumped) {
        sFillDumped = 1;
        vu32 *f = (vu32 *)ADDR_FILL_FN;
        for (u32 i = 0; i < 64; i++) {
            char t[8];
            t[0] = 'F'; t[1] = 'L'; t[2] = 'D';
            t[3] = (char)('0' + (i >> 4));
            t[4] = (char)('0' + (i & 0xF));
            t[5] = 0;
            TtyLog(t, f[i * 2], f[i * 2 + 1]);
        }
        /* BG 寄存器快照：確定填充層→SBB 映射與 DISPCNT */
        TtyLog("BGRA",
               (u32)*(vu16 *)0x04000008 | ((u32)*(vu16 *)0x0400000A << 16),
               (u32)*(vu16 *)0x0400000C | ((u32)*(vu16 *)0x0400000E << 16));
        TtyLog("BGRB",
               (u32)*(vu16 *)0x04000000,
               (u32)*(vu16 *)0x04001000);
    }
    /* v7.6.18：一次性轉儲 fill 支撐數據（靜態表 + 兩個 helper 真身）。
     * fill(id,x0,y0,w,h,pal) 語義（FLD 解碼定案）：
     *   entry = *(u32*)0x021467E4 + id*44；r6=[entry+8]=閃塊寫入的 BG map 指針；
     *   idx = helper2(x, y, entry[0x1D])（0x02040D38）；map[idx] 僅改 pal 高 4 位。
     * 確認閃塊 = fill(2,rect…)+fill(1,rect…) pal3；模式指示塊 = fill(3,X=FC*2+3,…)。
     * 指示塊（entry3）真機正確、閃塊（entry1/2）+16px → entry1/2/3 的 map 指針、
     * 平面字節 [entry+0x1D] 與 helper2 的座標映射是剩餘全部嫌疑。 */
    static u8 sMetaDumped;
    if (!sMetaDumped) {
        sMetaDumped = 1;
        u32 tblBase = *(vu32 *)ADDR_FILL_ENTRY_TBL;
        TtyLog("TBLD", tblBase, 0);
        for (u32 e = 0; e < 6; e++) {
            vu8 *en = (vu8 *)(tblBase + e * 44);
            char t[8];
            t[0] = 'T'; t[1] = 'B'; t[2] = (char)('0' + e); t[3] = 0;
            TtyLog(t, *(vu32 *)(en + 8), *(vu32 *)(en + 0x18));
        }
        /* helper2 (x,y,plane)->map idx：+16px 嫌疑核心（0xC0B = 24 條） */
        vu32 *h2 = (vu32 *)ADDR_HELPER2;
        for (u32 i = 0; i < 24; i++) {
            char t[8];
            t[0] = 'H'; t[1] = '2';
            t[2] = (char)('0' + (i >> 4)); t[3] = (char)('0' + (i & 0xF));
            t[4] = 0;
            TtyLog(t, h2[i * 2], h2[i * 2 + 1]);
        }
        /* helper1（寫 [sp+8]/[sp+9] 邊界）0x60B = 12 條 */
        vu32 *h1 = (vu32 *)ADDR_HELPER1;
        for (u32 i = 0; i < 12; i++) {
            char t[8];
            t[0] = 'H'; t[1] = '1';
            t[2] = (char)('0' + (i >> 4)); t[3] = (char)('0' + (i & 0xF));
            t[4] = 0;
            TtyLog(t, h1[i * 2], h1[i * 2 + 1]);
        }
    }
    /* v7.6.19：mode==2 確認閃塊時刻一次性轉儲 fill 寫目標 RAM map buffer。
     * v7.6.18 收斂：rect→fill 入參鏈全正確（反彙編+日誌雙實錘），+16px 唯一
     * 存活假設 = entry1/2 的 RAM map buffer（0x022AAFF8/0x022AB824）快照為
     * 原生（未右移）佈局——閃幀 memcpy 蓋回 VRAM 時鍵盤整體回原生位、亮塊
     * 在正確 rect 位 → 相對右移貼圖恆 +16px（模式格 tx3-12 原生==右移不受
     * 影響，與四格全部 +16px、懸停/貼圖全正常完全吻合）。
     * 本轉儲在渲染器（含入口 fill(2,0,0,32,24,pal1) 全 map pal 復位）之前
     * 執行，抓到的是閃幀寫入前的緩衝原貌 + SBB4/SBB2 顯示層同刻對照。
     * TB 轉儲 v2 的 [en+0x18] 全 0 沒抓到平面字節 → FBx 補 [en+0x1D..0x20]。
     * ExRR：map offset 0x4C0-0x5DF（stride32 rows19-22 完整 4 行，row23=欄
     * 底邊框行、v7.6.16 日誌全零不取），128 u16 → 64 條/entry。0x82C 項距=0x800 map+0x2C 強烈暗示 32 寬 → 平面 1/2。 */
    static u8 sFlashMapDumped;
    if (!sFlashMapDumped && a1 == 2) {
        sFlashMapDumped = 1;
        u32 tblBase2 = *(vu32 *)ADDR_FILL_ENTRY_TBL;
        for (u32 e = 1; e < 4; e++) {
            vu8 *en = (vu8 *)(tblBase2 + e * 44);
            vu16 *mp = *(vu16 **)(en + 8);
            char t[8];
            t[0] = 'F'; t[1] = 'B'; t[2] = (char)('0' + e); t[3] = 0;
            TtyLog(t, *(vu32 *)(en + 0xC),
                   (u32)en[0x1D] | ((u32)en[0x1E] << 8) |
                   ((u32)en[0x1F] << 16) | ((u32)en[0x20] << 24));
            if ((u32)mp >= 0x02000000u && (u32)mp < 0x03000000u) {
                for (u32 i = 0; i < 64; i++) {
                    vu16 *w = mp + 0x260 + i * 2;   /* offset 0x4C0 起 */
                    t[0] = 'E'; t[1] = (char)('0' + e);
                    t[2] = (char)('0' + (i >> 4)); t[3] = (char)('0' + (i & 0xF));
                    t[4] = 0;
                    TtyLog(t, (u32)w[0], (u32)w[1]);
                }
            }
        }
        /* SBB4（欄A 顯示層 0x06002000）rows19-23 同刻對照 */
        vu16 *mapA = (vu16 *)MB_MAP_BASE;
        for (int r = 19; r <= 23; r++) {
            vu16 *row = mapA + r * 32;
            for (int c = 0; c < 32; c += 2) {
                char t[8];
                t[0] = 'M'; t[1] = 'A'; t[2] = '4'; t[3] = 'R';
                t[4] = (char)('0' + r / 10); t[5] = (char)('0' + r % 10);
                t[6] = 0;
                TtyLog(t, (u32)row[c], (u32)row[c + 1]);
            }
        }
        /* SBB2（欄B 顯示層 0x06001000）rows19-23 同刻對照 */
        vu16 *mapB2 = (vu16 *)MB_MAP_BASE_B;
        for (int r = 19; r <= 23; r++) {
            vu16 *row = mapB2 + r * 32;
            for (int c = 0; c < 32; c += 2) {
                char t[8];
                t[0] = 'M'; t[1] = 'A'; t[2] = '2'; t[3] = 'R';
                t[4] = (char)('0' + r / 10); t[5] = (char)('0' + r % 10);
                t[6] = 0;
                TtyLog(t, (u32)row[c], (u32)row[c + 1]);
            }
        }
    }
    return ((u32 (*)(u32, u32, u32, u32, u32))RLOG_RENDERER)(a0, a1, a2, a3, a4);
}

__attribute__((used))
u32 OvHook_RLogA(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    return FlashRenderLog(0, a0, a1, a2, a3, a4);
}

__attribute__((used))
u32 OvHook_RLogB(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    return FlashRenderLog(1, a0, a1, a2, a3, a4);
}

/* v7.6.16：真機實錘站點 A/B（0x021F30A2/30CE）只在鍵盤初始化各觸發一次
 * （RLA fc=0/col=0/row=0/mode=1 後全程靜默）——每幀懸停/確認渲染走其餘
 * 7 個外部 bl 站點。反彙編核對全部同約定：ldr [obj+0xFC]; str [sp]（棧
 * 參=FC）、r0=obj+0x20、r1=mode（3618/365E/373A/398E/3A48=1、
 * 3880/3B50=2）、r2=rect、r3=[obj+0x100]。站點 C-I 全部接入觀測。 */
__attribute__((used))
u32 OvHook_RLogC(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    return FlashRenderLog(2, a0, a1, a2, a3, a4);
}
__attribute__((used))
u32 OvHook_RLogD(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    return FlashRenderLog(3, a0, a1, a2, a3, a4);
}
__attribute__((used))
u32 OvHook_RLogE(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    return FlashRenderLog(4, a0, a1, a2, a3, a4);
}
__attribute__((used))
u32 OvHook_RLogF(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    return FlashRenderLog(5, a0, a1, a2, a3, a4);
}
__attribute__((used))
u32 OvHook_RLogG(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    return FlashRenderLog(6, a0, a1, a2, a3, a4);
}
__attribute__((used))
u32 OvHook_RLogH(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    return FlashRenderLog(7, a0, a1, a2, a3, a4);
}
__attribute__((used))
u32 OvHook_RLogI(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    return FlashRenderLog(8, a0, a1, a2, a3, a4);
}
