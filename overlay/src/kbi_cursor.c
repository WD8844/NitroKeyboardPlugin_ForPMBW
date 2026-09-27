#include "kbi_internal.h"

/* ============================================================
 * v5.2：模式欄括號重映射 + A 鍵呼出「中」
 *  - 括號 = 引擎A OAM idx9-12 四角件（a1 bit12/13 = H/V 翻轉）。
 *    左角 X：20/36/52/68/84（五模式格）→ 100（返口寬鍵）→ 164（完成）
 *  - 「中」格遊戲邏輯上不存在（疊在返口寬鍵左端 x104-120），
 *    返口格依來源二態：來自格5/網格左半（prev≤90）→ 中模式（A 呼出）；
 *    來自完成/網格第6列以上（prev>90）→ 退格模式（A 原生退格）。
 *  - v5.2 實證（v5.1 日誌）：靜止動畫不止「呼吸」兩幀——還有「壓彈」
 *    幀（y9=177/178、x9=101/102、tile 0x0C→0x0D/0x0E，角件下移交叉，
 *    週期性出現）。v5.1 精確匹配 y∈{157,159} 被壓彈幀週期性擊穿 →
 *    CURM 刷屏、A 攔截隨壓彈相位規律交替、右角伸縮。
 *    → 寬帶檢測（y9∈[157,181] 且 y10∈[155,181]、x9∈[98,120]）+ 滯回
 *    （連續 12 次未命中才判離格）。y9 下界 157 同時排除假名網格
 *    第4列（y9=153/155、col7 x=108 會誤觸發）。
 *  - prev（來源格）只在「靜止/呼吸」x 上更新：16px 格距
 *    m=(x-12)%16 ∈{0..3}=網格列（須 y 在網格帶）/{8..11}=模式欄格
 *    （須 y 在模式欄帶）；滑動幀與壓彈幀一律不更新 → 中途值不污染
 *    來源判定（v5.1 的 prev 卡 84 / 垂直進入 stale 164 皆此因）。
 *    x∈[98,120]（返口帶）不更新 prev。
 *  - v5.3 實證（v5.2 日誌）：遊戲從影子緩衝讀括號座標維護行×列
 *    光標狀態（10 列 x=20..164）。退格/完成右移 16px 後括號落在
 *    116(第7列)/180(非法) → 遊戲幾何校驗失敗，方向鍵全鎖死；
 *    整體 hw→影子回拷也會踩掉遊戲寫入影子的動畫/邏輯值。
 *    → 回退 v5.1 寫入紀律：只在中模式寫 idx11/12（100-116 是合法
 *    第6列框），且影子僅在守衛通過時同步這兩個欄位。
 *    退格/完成維持原生括號（16px 視覺偏移，對齊需改遊戲條目座標表）。
 * ============================================================ */
#define BR_Y_TOP    157
#define BR_Y_BOT    179
#define BR_X_BS_L   100   /* 返口格左角 X（=格5右角，寬鍵 x104-168）*/
#define BR_X_BS_R   164   /* 返口格右角 X */
#define BR_X_ZH_R   116   /* 中格右角 X（120-4，同假名格 4px 內縮）*/
#define NAM_SNAP    16

s16 gBrPrevX;      /* 最近停留格的左角 X（來源判定，0=初始→中）*/
u8  gBrOnBS;       /* 滯回鎖定：停在返口格（A 攔截語義用）*/
u8  gBrZhongMode;  /* 返口格二態：1=中（A 呼出）0=退格（A 原生）*/
u8  gBrMiss;       /* 連續未命中採樣數（滯回計數）*/
u16 gBrLastX9;     /* 最近一幀左角 X（帶外按鍵診斷 BTOGX 用）*/

/* v5.5：模式欄條目表一次性掃描（用戶方案：模仿前5小格把「中」插到退格前）。
 * bad.bin 是開機快照（namein 未加載，KBEC 實例全零）→ 靜態掃描無效，
 * 必須在 namein 運行時掃主存。條目表可能形態：
 *  IT1 u16 x 列表 {20,36,52,68,84,100,164}      IT2 u32 x 列表
 *  IT3 u8  x 列表                                IT4 u16 寬列表 {16×5,64,64}
 *  IT5 u32 寬列表                                IT6 u8 左列號 {0,1,2,3,4,5,9}
 *  IT7 u8 右列號 {1,2,3,4,5,9,13}（右角=左角+寬 → (164-20)/16=9、(228-20)/16=13）
 *  IT8 u32 (100,164) 相鄰對（寬鍵條目結構碎片）
 *  IT13 f32 對 100.0f(0x42C80000)..164.0f(0x43240000)（UI 節點浮點座標）
 *  IT14 u16 對 (100,164)（IT8 只掃了 u32，u16 對漏網）
 * v5.6 實證：12 種編碼 × 4MB × 3 輪全零 → 表不存在，幾何為純代碼計算。
 * v5.7：補 IT13/IT14 最後兩種編碼，輪次上限收縮為 1（零命中不再重掃，
 * 消除用戶報告的「每次進模式欄卡頓零點幾秒」——那就是 4MB 掃描本身）。
 * 若 IT13/IT14 仍零 → 條目表路線正式關閉，轉向逆向 ov194 移動代碼。 */
static u8 gItblDone;
static u8 gItblRound;   /* 掃描輪次（0 起）；v5.7 起最多 1 輪 */
static u8 gItblZero;
static void ScanModeItemTable(void) {
    if (gItblDone) return;
    if (!gItblDone) gItblRound = 0;
    gItblDone = 1;
    gItblRound++;
    const vu8 *ram = (const vu8 *)0x02000000;
    u32 hits = 0;
    for (u32 a = 0; a < 0x400000 - 28; a++) {
        const vu8 *p = ram + a;
        /* 稀有字節前置守衛，壓低每字節均攤成本（避免首進模式欄明顯卡頓）*/
        if (p[0] == 20) {   /* IT1/IT2/IT3 首元素 */
            u32 ok16 = 1, ok32 = 1, ok8 = 1;
            for (u32 k = 1; k < 7; k++) {
                u32 v = 20 + 16 * k + (k == 6 ? 48 : 0);   /* 20,36,...,100,164 */
                if (p[k]     != (u8)v)  ok8  = 0;
                if (p[k*2]   != (u8)v || p[k*2+1] != 0) ok16 = 0;
                if (p[k*4]   != (u8)v || p[k*4+1] || p[k*4+2] || p[k*4+3]) ok32 = 0;
            }
            if (ok8)  { TtyLog("IT3", a + 0x02000000, 0); if (++hits > 24) return; }
            if (ok16) { TtyLog("IT1", a + 0x02000000, 0); if (++hits > 24) return; }
            if (ok32) { TtyLog("IT2", a + 0x02000000, 0); if (++hits > 24) return; }
        }
        if (p[1] == 16 && p[0] == 16) {   /* IT4/IT5 寬列表 */
            u32 ok16 = 1, ok32 = 1;
            for (u32 k = 2; k < 7; k++) {
                u32 v = (k < 5) ? 16 : 64;
                if (p[k*2] != v || p[k*2+1]) ok16 = 0;
                if (p[k*4] != v || p[k*4+1] || p[k*4+2] || p[k*4+3]) ok32 = 0;
            }
            if (ok16) { TtyLog("IT4", a + 0x02000000, 0); if (++hits > 24) return; }
            if (ok32) { TtyLog("IT5", a + 0x02000000, 0); if (++hits > 24) return; }
        }
        if (p[6] == 9 && p[0] == 0 && p[1] == 1 && p[2] == 2 && p[3] == 3 &&
            p[4] == 4 && p[5] == 5) {
            TtyLog("IT6", a + 0x02000000, 0); if (++hits > 24) return;
        }
        if (p[5] == 9 && p[0] == 1 && p[1] == 2 && p[2] == 3 && p[3] == 4 &&
            p[4] == 5 && p[6] == 13) {
            TtyLog("IT7", a + 0x02000000, 0); if (++hits > 24) return;
        }
        if (p[4] == 164 && p[0] == 100 && p[1] == 0 && p[2] == 0 &&
            p[3] == 0 && p[5] == 0 && p[6] == 0 && p[7] == 0) {
            TtyLog("IT8", a + 0x02000000, 0); if (++hits > 24) return;
        }
        /* v5.6：fx32（<<12）是 NNS G2D 布局標準編碼——20<<12=0x14000 等，
         * 7 個值低半字全為 0x4000、字節 2 遞增（01,02,03,04,05,06,0A）*/
        if (p[1] == 0x40 && p[0] == 0 && p[2] == 0x01) {   /* IT9 fx32 x 表 */
            u32 ok = 1;
            for (u32 k = 1; k < 7; k++) {
                u32 v = (20 + 16 * k + (k == 6 ? 48 : 0)) << 12;
                if (p[k*4] != (u8)v || p[k*4+1] != (u8)(v >> 8) ||
                    p[k*4+2] != (u8)(v >> 16) || p[k*4+3] != (u8)(v >> 24)) ok = 0;
            }
            if (ok) { TtyLog("IT9", a + 0x02000000, 0); if (++hits > 24) return; }
        }
        if (p[0] == 0xC8 && p[1] == 0 && p[2] == 0x68 && p[3] == 0x01) {
            /* IT10 u16 半定點（<<4）：200,360,520,680,840,1000,1640 */
            u32 ok = 1;
            for (u32 k = 1; k < 7; k++) {
                u32 v = 200 + 160 * k + (k == 6 ? 640 : 0);
                if (p[k*2] != (u8)v || p[k*2+1] != (u8)(v >> 8)) ok = 0;
            }
            if (ok) { TtyLog("IT10", a + 0x02000000, 0); if (++hits > 24) return; }
        }
        if (p[1] == 0 && p[0] == 0x14 && p[3] == 0 && p[2] >= 140 && p[2] <= 200) {
            /* IT11 u16 (x,y) 座標對（y∈[140,200] 模式欄行）*/
            u32 ok = 1;
            for (u32 k = 1; k < 7; k++) {
                u32 x = 20 + 16 * k + (k == 6 ? 48 : 0);
                if (p[k*4] != (u8)x || p[k*4+1] != 0 ||
                    p[k*4+2] < 140 || p[k*4+2] > 200 || p[k*4+3] != 0) ok = 0;
            }
            if (ok) { TtyLog("IT11", a + 0x02000000, 0); if (++hits > 24) return; }
        }
        if (p[0] == 0 && p[1] == 0x40 && p[2] == 0x01 && p[3] == 0 &&
            p[4] == 0 && p[6] == 0 && p[7] == 0 && p[5] >= 0x8C && p[5] <= 0xC8) {
            /* IT12 fx32 (x,y) 座標對（y<<12 ∈ [140,200]<<12）*/
            u32 ok = 1;
            for (u32 k = 1; k < 7; k++) {
                u32 x = (20 + 16 * k + (k == 6 ? 48 : 0)) << 12;
                if (p[k*8] != (u8)x || p[k*8+1] != (u8)(x >> 8) ||
                    p[k*8+2] != (u8)(x >> 16) || p[k*8+3] != 0 ||
                    p[k*8+4] != 0 || p[k*8+5] < 0x8C || p[k*8+5] > 0xC8 ||
                    p[k*8+6] != 0 || p[k*8+7] != 0) ok = 0;
            }
            if (ok) { TtyLog("IT12", a + 0x02000000, 0); if (++hits > 24) return; }
        }
        /* v5.7：IT13 f32 座標（UI 節點常用 float）——100.0f 後 64B 內出現 164.0f */
        if (p[0] == 0 && p[1] == 0 && p[2] == 0xC8 && p[3] == 0x42) {
            const vu8 *q = p + 4;
            for (u32 k = 0; k < 64 - 4; k++, q++) {
                if (q[0] == 0 && q[1] == 0 && q[2] == 0x24 && q[3] == 0x43) {
                    TtyLog("IT13", a + 0x02000000, 0);
                    if (++hits > 24) return;
                    break;
                }
            }
        }
        /* v5.7：IT14 u16 對 (100,164)——補 IT8 的 u32 盲區 */
        if (p[0] == 100 && p[1] == 0 && p[2] == 164 && p[3] == 0) {
            TtyLog("IT14", a + 0x02000000, 0); if (++hits > 24) return;
        }
    }
    if (!hits) { gItblZero = 1; TtyLog("IT0", gItblRound, 0); }   /* arg1=輪次 */
    else gItblZero = 0;
}
u16 gSnapLen;
static u16 gSnapChars[NAM_SNAP];
u32 gSnapHeap;
u8  gRestoreArmed;

/* 進入中模式時快照命名輸入框（A 可能同幀觸發遊戲退格，用於復原）。
 * gSnapHeap 緩存 + 魔數/終止符校驗，重進入時免 4MB 重掃（v5.1）*/
void SnapNaming(void) {
    u32 heap = gSnapHeap;
    bool ok = heap && (*(vu32 *)heap == FND_HEAP_MAGIC);
    if (ok) {
        u16 len = *(u16 *)(heap + NAMING_LEN_OFFSET);
        if (len > NAM_SNAP ||
            *(u16 *)(heap + NAMING_BUF_OFFSET + len * 2) != 0xFFFF) ok = false;
    }
    if (!ok) { heap = FindNamingHeap(); gSnapHeap = heap; }
    gSnapLen = 0;
    if (heap) {
        u16 len = *(u16 *)(heap + NAMING_LEN_OFFSET);
        if (len <= NAM_SNAP) {
            gSnapLen = len;
            for (u16 k = 0; k < len; k++)
                gSnapChars[k] = *(u16 *)(heap + NAMING_BUF_OFFSET + k * 2);
        }
    }
    TtyLog("SNAP", gSnapLen, heap);
}

/* 取消鍵盤時復原快照（若遊戲已同幀執行退格則撤銷刪字）*/
void RestoreNamingSnap(void) {
    gRestoreArmed = 0;
    if (gSnapHeap && gSnapLen) {
        for (u16 k = 0; k < gSnapLen; k++)
            *(u16 *)(gSnapHeap + NAMING_BUF_OFFSET + k * 2) = gSnapChars[k];
        *(u16 *)(gSnapHeap + NAMING_LEN_OFFSET) = gSnapLen;
        *(u16 *)(gSnapHeap + NAMING_BUF_OFFSET + gSnapLen * 2) = 0xFFFF;
        TtyLog("AREST", gSnapLen, gSnapHeap);
    }
}

/* 每幀調用：括號狀態機 + 重映射（硬體 OAM + 影子緩衝雙寫）
 * v5.2：寬帶檢測 + 滯回 + 靜止值 prev + 退格/完成整體右移 16px。
 * 硬體 OAM 每幀 vblank DMA 後為遊戲原生值，讀到即原生狀態；
 * 我們寫入的位移值經影子緩衝持久，值域互斥保證冪等。 */
void ApplyCursorRemap(void) {
    vu16 *oam = (vu16 *)0x07000000;   /* 引擎A OAM（u16 步進 4）*/
    u32 y9  = oam[9 * 4]     & 0xFF;
    u32 y10 = oam[10 * 4]    & 0xFF;
    u32 x9  = oam[9 * 4 + 1] & 0x1FF;
    gBrLastX9 = (u16)x9;
    bool yBand = (y9 >= 157 && y9 <= 181) && (y10 >= 155 && y10 <= 181);
    bool onBS  = yBand && x9 >= 98 && x9 <= 120;

    if (onBS) {
        gBrMiss = 0;
        if (!gBrOnBS) {
            gBrOnBS = 1;
            /* 來源：格1-5/網格左半（≤90）→ 中；完成（163-183）/
             * 網格第6列以上（≥92）→ 退格 */
            gBrZhongMode = (gBrPrevX <= 90);
            if (gBrZhongMode) SnapNaming();
            TtyLog("CURM", gBrZhongMode, gBrPrevX);
            ScanModeItemTable();   /* v5.5：首次到達模式欄時一次性掃描條目表 */
        }
    } else {
        /* 滯回：壓彈幀在帶內不會走到這；連續 12 次未命中才判離格 */
        if (++gBrMiss >= 12) {
            if (gBrOnBS) TtyLog("CURL", gBrPrevX, x9);
            gBrOnBS = 0;
        }
        /* 來源格記錄：僅「靜止/呼吸」x（16px 格距、呼吸+2），
         * 網格列值須 y 在網格帶、模式欄值須 y 在模式欄帶 →
         * 滑動幀（值對不上帶）與壓彈幀（onBS 分支）皆不污染 */
        if (x9 >= 12 && x9 <= 168 && !(x9 >= 98 && x9 <= 120)) {
            u32 m = (x9 - 12) & 15;
            if (m <= 3) {
                if (y9 >= 97 && y9 <= 153 && y10 >= 97 && y10 <= 153)
                    gBrPrevX = (s16)x9;          /* 網格列（col1-10）*/
            } else if (m >= 8 && m <= 11) {
                if (yBand) gBrPrevX = (s16)x9;   /* 模式欄格/完成 */
            }
        }
    }

    /* --- 重映射（v5.3：只做中模式右角，v5.1 寫入紀律）---
     * v5.2 實證：遊戲從影子緩衝讀括號座標維護「行×列」光標狀態
     * （10 列 x=20..164 間距16，100=第6列返口、164=第10列完成）。
     * 退格/完成整體右移 16px 後括號落在 116(第7列)/180(非法)，
     * 遊戲幾何校驗不過 → 方向鍵全鎖死（上下換行邏輯不受限，尚可逃離，
     * 且逃離後括號以我們寫入的 116 為新列基準重現於網格第7列）。
     * 另：整體 hw→影子回拷會在壓彈/滑動幀踩掉遊戲寫入影子的
     * 動畫/邏輯值，同樣致命 —— 影子只准寫我們重映射的欄位、
     * 只准在守衛通過（原生寬鍵間距）時寫。 */
    if (!yBand) return;
    if (onBS && gBrZhongMode) {
        /* 中模式：右角 → 左角+12（v5.4 對齊微調：+16 時用戶實測右側
         * 超長一點）。右角不是遊戲讀取的欄位（v5.3 實證），可自由調 */
        u32 xl = x9 + 12;
        for (int k = 0; k < 2; k++) {
            vu16 *hp = &oam[(11 + k) * 4 + 1];
            u32 d = (*hp & 0x1FF) - x9;
            if (d >= 60 && d <= 68) {   /* 原生寬鍵間距（含壓彈 ±2）*/
                u16 v = (u16)((*hp & 0xF000) | xl);
                *hp = v;
                if (gShadowAddr && gShadowAddr != 0xFFFFFFFF)
                    *(vu16 *)((vu16 *)gShadowAddr + (11 + k) * 4 + 1) = v;
            }
        }
    }
    /* 退格/完成模式：不寫 OAM/影子（原生 100-164/164-228 是合法列框，
     * 僅 16px 視覺偏移；对齊需改遊戲條目座標表，暫緩）*/
}
/* ============================================================
 * v6.3：STAT 掃描整體靜默（STAT_SCAN_ENABLED=0）
 *  教訓（no$gba 斷點實證 2026-09-09）：StatSample 的候選過濾循環
 *  每次採樣把全部候選地址重讀一遍（02215556-02215A50 一帶），no$gba
 *  的 [addr]?/! 斷點全停在我們自己的讀取上——0214BCA4/0214BA24/
 *  021467A4 的「命中」無一例外 PC∈[02213C00,02216470]。
 *  → 斷點調試期必須徹底剔除掃描，让 no$gba 只看得到遊戲的訪問。
 *  掃描代碼原樣保留，翻回 1 即可恢復 v6.2 行為。
 * ============================================================ */
/* v7：ov194 原生「中」格停靠啟用（配套 _patch_ov194.py + OvHook_* 鉤子）。
 * 啟用時：v5 的 L/R 手動切換、A 劫持、括號 OAM 重映射全部退役
 * （遊戲原生 col5=中，括號由補丁後 get-rect 直接給出 100/116）。 */
#if STAT_SCAN_ENABLED
/* ============================================================
 * v5.9→v6.0：差分掃描定位光標狀態變量（STAT）
 *  v6.0（v5.9 真機日誌教訓）：①x/y 寬區間誤留 36+352 假陽性（8 個
 *  STAA 全是 [320,3920) 非 16 倍數噪聲）→ 改精確停靠集過濾；②換區
 *  128-256KB 單幀收集 = 用戶感知卡頓 → 改每幀 16KB 增量泵；③STAA
 *  tag 與地址高位重疊無法分類 → tag 移到 bit28+；④STAA 上限 8→12
 *  v5.8 實測教訓（真機日誌）：①括號為平滑滑動（OAMM 出現 x=133/147
 *  等中間值）→ 固定「按鍵後 2 幀」採樣會撞滑動瞬時值誤殺真候選
 *  （區域1 有 4→1→0 的收斂痕跡）；②u8 候選在全部區域撞 1024 上限
 *  截斷；③狀態可能以像素/定點形式存在而非列號枚舉。
 *  v5.9 對策：
 *   - 穩定門控採樣：括號 x9 連續 ≥4 幀 |Δ|≤2（容忍呼吸±2）才採樣，
 *     90 幀超時放棄（替代固定延遲）
 *   - 值域放寬：x 族 [20,244]∪<<4定點[320,3920]；y 族 [57,187]∪[912,3000]
 *   - 行/列兩族行為規則：H 族（列/x）水平必變垂直必不變；
 *     V 族（行/y）垂直必變水平必不變
 *   - u8/u16 小整數類（≤15）取代停靠集枚舉；lo/hi 跨度輔助判定
 *   - 區域階梯 8 級：影子 OAM ±4K 起逐環外擴（含 arm9 靜態區）
 *  類：C8H/C8V(u8)、C16c(u16 小整數)、C16x(x族)、C16y(y族)
 *  日誌：STAT arg1=n8H|n8V<<10|nC<<20 / STAT2 arg1=nX|nY<<12 /
 *       STAT3 arg1=n32X|n32Y<<12（存活數），arg2=vert<<15|region<<24|sample；
 *       STAA arg1=addr|tag(0=C8H,0x40000000=C8V,0x10000000=C16c,
 *       0x20000000=C16x,0x30000000=C16y,0x50000000=C32X,0x60000000=C32Y)、
 *       arg2=當前值；STRG=換區
 * v6.1 修正（v6.0 日誌：30 步四方向、R0-R1 全滅、樣本1即死）：
 *  - 全滅根因=零值氾濫：StatSmallOk 收 0 → 零填充內存灌滿 512 cap →
 *    零永不變 → 第一個水平樣本全滅（n8H 512→0 實證）。收集一律跳 0
 *  - 收集延遲到水平樣本且括號列≥1（列變量此時必非零）
 *  - 區域表補 ov10 data+bss（0x02154E60-0x02172A20，122KB）/ov194
 *    （0x021F1320-0x021F5620）/ov10 後堆——鍵盤狀態最可能在 namein overlay
 *  - 新增 u32 x/y 類（4 對齊，精確停靠集）；libgcc 除法/取模禁用不變
 * v6.2（v6.1 日誌：R0 u8H 512→5 真收斂、R1 35/35、R2 剛收集即止；仍零存活）：
 *  - 值域 ≤15→≤59：覆蓋線性格索引（行×10+列）編碼
 *  - STAA 門檻 hcnt≥6→3：v6.1 的 5 個倖存者因門檻太高沒記地址就死了
 * ============================================================ */
#define STAT_CAPH  512    /* u8/u16 小整數類容量 */
#define STAT_CAPX  1024   /* x/y 族容量 */
/* v6.2：值域放寬到 ≤59——同時覆蓋「列號 0..9」與「線性格索引 行×10+列
 * 0..59」兩種編碼（v6.1 日誌：u8H 512→5 真收斂出現但 5 個倖存者因
 * STAA 門檻 hcnt≥6 沒被記錄就死了；值域 ≤15 收不到格索引編碼）*/
static bool StatSmallOk(u32 v) { return v != 0 && v <= 59; }
/* v6.0：精確停靠集過濾（v5.9 寬區間實測誤留 36 個 x/352 個 y 假陽性，
 * 8 個 STAA 全是 [320,3920) 區間裡的非 16 倍數噪聲）。
 * x：像素停靠 20,36,...,164（呼吸+2 → 22..166）或其 <<4 定點形式 */
static bool StatXLikeOk(u32 v) {
    if (v >= 320 && (v & 15) == 0) v >>= 4;   /* <<4 定點 → 像素 */
    return v >= 20 && v <= 166 && (((v - 20) & 15) <= 2);
}
/* y：網格行 61,85,109,133,157（±2 呼吸）∪ 模式欄 157/159/177/178/179。
 * 注意：禁用 %（libgcc __aeabi_uidivmod 被 overlay 鏈接 discard）*/
static bool StatYLikeOk(u32 v) {
    if (v >= 512 && (v & 15) == 0) v >>= 4;
    if (v >= 59 && v <= 159) {
        u32 r = v - 59;
        while (r >= 24) r -= 24;
        if (r <= 4) return true;
    }
    return v == 157 || v == 159 || v == 177 || v == 178 || v == 179;
}

typedef struct {
    u32 *addr; u16 *last; u16 *mask; u8 *alo; u8 *ahi;
    u32 n, cap;
    u8 vert;                        /* 1=V 族：垂直變水平不變 */
    u8 wide;                        /* 0=u8 1=u16 2=u32 讀寬度 */
    bool (*ok)(u32);
    u32 tag;
} StatClass;

static u32 gS8Ha[STAT_CAPH]; static u16 gS8Hl[STAT_CAPH]; static u16 gS8Hm[STAT_CAPH];
static u8 gS8Hlo[STAT_CAPH]; static u8 gS8Hhi[STAT_CAPH];
static u32 gS8Va[STAT_CAPH]; static u16 gS8Vl[STAT_CAPH]; static u16 gS8Vm[STAT_CAPH];
static u8 gS8Vlo[STAT_CAPH]; static u8 gS8Vhi[STAT_CAPH];
static u32 gSca[STAT_CAPH];  static u16 gScl[STAT_CAPH];  static u16 gScm[STAT_CAPH];
static u8 gSclo[STAT_CAPH];  static u8 gSchi[STAT_CAPH];
static u32 gSxa[STAT_CAPX];  static u16 gSxl[STAT_CAPX];  static u16 gSxm[STAT_CAPX];
static u8 gSxlo[STAT_CAPX];  static u8 gSxhi[STAT_CAPX];
static u32 gSya[STAT_CAPX];  static u16 gSyl[STAT_CAPX];  static u16 gSym[STAT_CAPX];
static u8 gSylo[STAT_CAPX];  static u8 gSyhi[STAT_CAPX];
#define STAT_CAP32 256
static u32 gS3xa[STAT_CAP32]; static u16 gS3xl[STAT_CAP32]; static u16 gS3xm[STAT_CAP32];
static u8 gS3xlo[STAT_CAP32]; static u8 gS3xhi[STAT_CAP32];
static u32 gS3ya[STAT_CAP32]; static u16 gS3yl[STAT_CAP32]; static u16 gS3ym[STAT_CAP32];
static u8 gS3ylo[STAT_CAP32]; static u8 gS3yhi[STAT_CAP32];

static StatClass gStatCls[7] = {
    { gS8Ha, gS8Hl, gS8Hm, gS8Hlo, gS8Hhi, 0, STAT_CAPH, 0, 0, StatSmallOk, 0x00000000 },
    { gS8Va, gS8Vl, gS8Vm, gS8Vlo, gS8Vhi, 0, STAT_CAPH, 1, 0, StatSmallOk, 0x40000000 },
    { gSca,  gScl,  gScm,  gSclo,  gSchi,  0, STAT_CAPH, 0, 1, StatSmallOk, 0x10000000 },
    { gSxa,  gSxl,  gSxm,  gSxlo,  gSxhi,  0, STAT_CAPX, 0, 1, StatXLikeOk, 0x20000000 },
    { gSya,  gSyl,  gSym,  gSylo,  gSyhi,  0, STAT_CAPX, 1, 1, StatYLikeOk, 0x30000000 },
    { gS3xa, gS3xl, gS3xm, gS3xlo, gS3xhi, 0, STAT_CAP32, 0, 2, StatXLikeOk, 0x50000000 },
    { gS3ya, gS3yl, gS3ym, gS3ylo, gS3yhi, 0, STAT_CAP32, 1, 2, StatYLikeOk, 0x60000000 },
};
#ifdef GAME_WHITE
/* 白版：ov 區全域 +0x20（ov10 載入基址 0x02154E80、ov194 0x021F1340）*/
static const u32 kStatRegions[8][2] = {
    {0x02146000, 0x02147000},   /* R0 影子 OAM ±4K 上下文 */
    {0x02147000, 0x02148000},   /* R1 */
    {0x02144000, 0x02146000},   /* R2 */
    {0x02148000, 0x0214C000},   /* R3 */
    {0x0214C000, 0x02154E80},   /* R4 至 ov10 載入基址 */
    {0x02154E80, 0x02172A40},   /* R5 ov10 data+bss（namein 主 UI，v6.1 新增）*/
    {0x021F1340, 0x021F5640},   /* R6 ov194（namein 邏輯，v6.1 新增）*/
    {0x02172A40, 0x02180000},   /* R7 ov10 後堆區（v6.1 新增）*/
};
#else
static const u32 kStatRegions[8][2] = {
    {0x02146000, 0x02147000},   /* R0 影子 OAM ±4K 上下文 */
    {0x02147000, 0x02148000},   /* R1 */
    {0x02144000, 0x02146000},   /* R2 */
    {0x02148000, 0x0214C000},   /* R3 */
    {0x0214C000, 0x02154E60},   /* R4 至 ov10 載入基址 */
    {0x02154E60, 0x02172A20},   /* R5 ov10 data+bss（namein 主 UI，v6.1 新增）*/
    {0x021F1320, 0x021F5620},   /* R6 ov194（namein 邏輯，v6.1 新增）*/
    {0x02172A20, 0x02180000},   /* R7 ov10 後堆區（v6.1 新增）*/
};
#endif
static u8  gStatPhase;     /* 0=未採樣 1=已武裝 */
static u8  gStatPend;      /* 1=等待括號穩定後採樣 */
static u8  gStatPendVert;
static u8  gStatStableCnt;
static u8  gStatTimeout;
static u16 gStatPrevX;
static u8  gStatRegion;
static u8  gStatHCnt, gStatVCnt, gStatSample;
static u16 gStatLastCol;

static u32 StatPop16(u16 v) {   /* 手寫 popcount（libgcc 版被 discard）*/
    u32 n = 0;
    while (v) { n += v & 1; v >>= 1; }
    return n;
}

static bool StatIsSurvivor(StatClass *c, u32 i) {
    /* v6.2：門檻降低——v6.1 實測 R0 的 5 個 u8H 倖存者撐了 2 個樣本
     * 就死了，hcnt≥6 的記錄門檻讓地址線索白白流失 */
    if (c->vert)   /* V 族：行變化次數少，門檻放低 */
        return gStatHCnt >= 3 && gStatVCnt >= 2 && StatPop16(c->mask[i]) >= 2;
    return gStatHCnt >= 3 && (StatPop16(c->mask[i]) >= 2 ||
                              (u32)(c->ahi[i] - c->alo[i]) >= 16);
}

static void StatLogSurvivors(void) {
    int logged = 0;
    for (int ci = 0; ci < 7 && logged < 12; ci++) {
        StatClass *c = &gStatCls[ci];
        for (u32 i = 0; i < c->n && logged < 12; i++) {
            if (StatIsSurvivor(c, i)) {
                TtyLog("STAA", c->addr[i] | c->tag, c->last[i]);
                logged++;
            }
        }
    }
}

/* v6.0：增量收集——v5.9 實測換區時 128-256KB 單幀掃完 = 用戶感知卡頓
 * （B 鍵操作時恰好撞上）。改為每幀最多 16KB，R3 需 16 幀（~0.27s），
 * 單幀開銷 <3ms，徹底消除卡頓 */
#define STAT_CHUNK 0x4000
static u32 gStatScanAddr;   /* 收集掃描當前地址（0=未啟動）*/
void StatCollectChunk(void) {
    u32 hi = kStatRegions[gStatRegion][1];
    u32 end = gStatScanAddr + STAT_CHUNK;
    if (end > hi) end = hi;
    for (u32 a = gStatScanAddr; a + 2 <= end; a += 2) {
        u16 v = *(vu16 *)a;
        u8 b0 = (u8)v, b1 = (u8)(v >> 8);
        StatClass *c;
        c = &gStatCls[0];
        if (c->n < c->cap && c->ok(b0)) {
            c->addr[c->n] = a; c->last[c->n] = b0; c->mask[c->n] = 0;
            c->alo[c->n] = b0; c->ahi[c->n] = b0; c->n++;
        }
        if (c->n < c->cap && c->ok(b1)) {
            c->addr[c->n] = a + 1; c->last[c->n] = b1; c->mask[c->n] = 0;
            c->alo[c->n] = b1; c->ahi[c->n] = b1; c->n++;
        }
        c = &gStatCls[1];
        if (c->n < c->cap && c->ok(b0)) {
            c->addr[c->n] = a; c->last[c->n] = b0; c->mask[c->n] = 0;
            c->alo[c->n] = b0; c->ahi[c->n] = b0; c->n++;
        }
        if (c->n < c->cap && c->ok(b1)) {
            c->addr[c->n] = a + 1; c->last[c->n] = b1; c->mask[c->n] = 0;
            c->alo[c->n] = b1; c->ahi[c->n] = b1; c->n++;
        }
        c = &gStatCls[2];
        if (c->n < c->cap && c->ok(v)) {
            c->addr[c->n] = a; c->last[c->n] = v; c->mask[c->n] = 0;
            c->alo[c->n] = (u8)v; c->ahi[c->n] = (u8)v; c->n++;
        }
        c = &gStatCls[3];
        if (c->n < c->cap && c->ok(v)) {
            c->addr[c->n] = a; c->last[c->n] = v; c->mask[c->n] = 0;
            c->alo[c->n] = (u8)(v & 0xFF); c->ahi[c->n] = (u8)(v >> 8); c->n++;
        }
        c = &gStatCls[4];
        if (c->n < c->cap && c->ok(v)) {
            c->addr[c->n] = a; c->last[c->n] = v; c->mask[c->n] = 0;
            c->alo[c->n] = (u8)(v & 0xFF); c->ahi[c->n] = (u8)(v >> 8); c->n++;
        }
        /* v6.1：u32 座標類（僅 4 對齊地址）*/
        if ((a & 3) == 0) {
            u32 v32 = *(vu32 *)a;
            c = &gStatCls[5];
            if (c->n < c->cap && c->ok(v32)) {
                c->addr[c->n] = a; c->last[c->n] = (u16)v32; c->mask[c->n] = 0;
                c->alo[c->n] = (u8)v32; c->ahi[c->n] = (u8)(v32 >> 8); c->n++;
            }
            c = &gStatCls[6];
            if (c->n < c->cap && c->ok(v32)) {
                c->addr[c->n] = a; c->last[c->n] = (u16)v32; c->mask[c->n] = 0;
                c->alo[c->n] = (u8)v32; c->ahi[c->n] = (u8)(v32 >> 8); c->n++;
            }
        }
    }
    gStatScanAddr = end;
    if (gStatScanAddr + 2 > hi) {
        gStatPhase = 1;
        TtyLog("STAT", gStatCls[0].n | (gStatCls[1].n << 10) | (gStatCls[2].n << 20),
               (u32)gStatRegion << 24);
        TtyLog("STAT2", gStatCls[3].n | (gStatCls[4].n << 12),
               (u32)gStatRegion << 24);
        TtyLog("STAT3", gStatCls[5].n | (gStatCls[6].n << 12),
               (u32)gStatRegion << 24);
    }
}

void StatSample(int vert) {
    u32 x9 = gBrLastX9;
    u32 col = 0xFF;
    if (x9 >= 20 && x9 < 180) col = (x9 - 20) >> 4;
    if (!vert) {
        if (col == 0xFF) return;                 /* 括號不在格點上（滑動幀）*/
        if (col == gStatLastCol) return;         /* 按鍵未換格（被拒/端點）*/
    }
    if (!gStatPhase) {
        /* v6.0：收集改為跨幀增量（StatCollectChunk 每幀泵入），
         * 本幀只啟動，收集完成後的下一次穩定門控才開始過濾。
         * v6.1：收集延遲到「水平樣本且括號列 ≥1」——列狀態變量此時
         * 必為非零值，配合 StatSmallOk 跳零徹底避開零值氾濫 */
        if (!gStatScanAddr) {
            if (vert || col == 0xFF || col == 0) return;
            for (int ci = 0; ci < 7; ci++) gStatCls[ci].n = 0;
            gStatScanAddr = kStatRegions[gStatRegion][0];
        }
        return;
    }
    {
        /* 過濾：H 族水平必變垂直必不變；V 族反之 */
        for (int ci = 0; ci < 7; ci++) {
            StatClass *c = &gStatCls[ci];
            u32 k = 0;
            for (u32 i = 0; i < c->n; i++) {
                u32 v = (c->wide == 2) ? *(vu32 *)c->addr[i]
                      : (c->wide == 1) ? *(vu16 *)c->addr[i]
                      : *(vu8 *)c->addr[i];
                bool changed = (v != c->last[i]);
                bool keep;
                if (c->vert) keep = vert ? changed : !changed;
                else         keep = vert ? !changed : changed;
                if (keep && changed) {
                    if (!c->ok(v)) continue;
                    c->mask[i] |= 1 << (v & 15);
                    if (v < c->alo[i]) c->alo[i] = (u8)v;
                    if (v > c->ahi[i]) c->ahi[i] = (u8)v;   /* u8 飽和，僅小值域類有意義 */
                }
                if (!keep) continue;
                c->last[i] = (u16)v;
                c->addr[k] = c->addr[i]; c->last[k] = c->last[i];
                c->mask[k] = c->mask[i]; c->alo[k] = c->alo[i]; c->ahi[k] = c->ahi[i];
                k++;
            }
            c->n = k;
        }
        gStatSample++;
        if (vert) gStatVCnt++; else gStatHCnt++;
        TtyLog("STAT", gStatCls[0].n | (gStatCls[1].n << 10) | (gStatCls[2].n << 20),
               (u32)(vert ? 0x8000 : 0) | ((u32)gStatRegion << 24) | gStatSample);
        TtyLog("STAT2", gStatCls[3].n | (gStatCls[4].n << 12),
               (u32)(vert ? 0x8000 : 0) | ((u32)gStatRegion << 24) | gStatSample);
        TtyLog("STAT3", gStatCls[5].n | (gStatCls[6].n << 12),
               (u32)(vert ? 0x8000 : 0) | ((u32)gStatRegion << 24) | gStatSample);
        /* 區域耗盡：≥8 個水平樣本仍零存活 → 換區 */
        u32 tot = 0;
        for (int ci = 0; ci < 7; ci++) tot += gStatCls[ci].n;
        if (gStatHCnt >= 8 && tot == 0) {
            gStatRegion = (gStatRegion + 1) % 8;
            gStatPhase = 0; gStatHCnt = 0; gStatVCnt = 0; gStatSample = 0;
            gStatScanAddr = 0;
            TtyLog("STRG", gStatRegion, 0);
        }
    }
    gStatLastCol = (u16)col;
    StatLogSurvivors();
}
#endif /* STAT_SCAN_ENABLED */
