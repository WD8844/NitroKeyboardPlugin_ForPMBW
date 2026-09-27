#include "kbi_internal.h"

/* ============================================================
 * 觸摸圖標觸發：遊戲鍵盤左下角模式格旁的「中」圖標
 * 與 kmod 相同方式讀 SDK 共享觸摸緩衝（ARM7 每幀更新），
 * 用遊戲自己的 TP_GetCalibratedPoint 校準座標。
 * 圖標繪製（VRAM 圖塊）為第二階段，先做觸摸觸發。
 * ============================================================ */
extern u8 HW_TOUCHPANEL_BUF[];       /* symbols.ld 解析 */
/* TP_GetCalibratedPoint 也在 symbols.ld */

struct SPITpData {
    u32 x:12;
    u32 y:12;
    u32 touch:1;
    u32 validity:2;
    u32 dummy:5;
};

/* 觸摸區域（螢幕座標）：模式欄新增第 6 格「中」（tx13-14）
 * x = 13*8 = 104..119，y = rows20-22 = 160..183（與原生格同高）*/
#define ICON_X   104
#define ICON_Y   160
#define ICON_W   16
#define ICON_H   24

bool gIconTriggered;
bool gTouchWasDown;
int  gTouchLastX, gTouchLastY;
static u16 gCurLastKeys;
static u32 gCurHoldCnt;

static void PollCursorScan(void) {
    if (!gBlockModeTouch || IsPluginKeyboardVisible()) {
        gCurLastKeys = 0; return;
    }
#if !V7_NATIVE_ZHONG
    ApplyCursorRemap();   /* v5：每幀括號重映射（v7 原生停靠後由遊戲接管）*/
#endif
#if STAT_SCAN_ENABLED
    /* v6.0：增量收集泵（每幀 ≤16KB，消除換區單幀大掃描卡頓）*/
    if (!gStatPhase && gStatScanAddr) StatCollectChunk();
    /* v5.9：穩定門控採樣——括號 x9 連續 ≥4 幀 |Δ|≤2（容忍呼吸±2）才
     * 採樣；90 幀超時放棄。滑動中的瞬時值不再誤殺候選（v5.8 教訓）*/
    if (gStatPend) {
        int d = (int)gBrLastX9 - (int)gStatPrevX;
        if (d < 0) d = -d;
        gStatPrevX = gBrLastX9;
        if (d <= 2) gStatStableCnt++; else gStatStableCnt = 0;
        if (++gStatTimeout > 90) gStatPend = 0;
        else if (gStatStableCnt >= 4) { gStatPend = 0; StatSample(gStatPendVert); }
    }
#endif /* STAT_SCAN_ENABLED */
    u16 keys = *(vu16 *)0x04000130;   /* KEYINPUT：低電平有效 */
    u16 down = (~keys) & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN | KEY_A | KEY_B |
                          KEY_SELECT | KEY_L | KEY_R);
    u16 edge = down ^ gCurLastKeys;
    gCurLastKeys = down;
    if (!edge) {
        /* 按住期間每 8 帧差分一次，捕捉自動連移 */
        if (down) { /* 按住期間無需操作（OAM 傾瀉僅在按鍵沿）*/ }
        return;
    }
    if (down & edge) TtyLog("KDOWN", down & edge, down);
    /* v7.5：B 退格交還遊戲原生鏈，插件不再插手。
     * v7.2-v7.4 的「E4=5 消費者在 ov194 外、寫入無效」結論作廢——真因是
     * _patch_ov194.py 按錯位解碼把分發器真 idx7（退格臂，E4=5）寫成公共
     * 出口，id7 事件（B 鍵 poll_event 找 id7 格 / 觸摸 col6 / A col6）全被
     * 吞，退格只能靠插件直改 EXPH 兜底：無聲（移動聲 0x548 是錯的，原生
     * 退格聲/空串錯誤聲由 E4=5 消費者播放）+ 每次 B 跑 4MB FindNamingHeap
     * = 掉幀卡頓。v7.5 恢復 @0x021F37DE 原生偏移後：
     *   B 鍵 / 返口觸摸 / 返口 A → id7 → E4=5 → 原生退格（含兩種音效）
     *   完成觸摸/A（col7）→ id8 → E4=7 → 原生完成提交
     *   中（col5）→ id10 → 郵箱臂 → 呼出插件鍵盤（不變） */
#if !V7_NATIVE_ZHONG
    /* v5.7：L/R = 手動切換 中/退格，【僅在寬鍵帶內生效】。
     * v5.6 日誌實證：①SELECT（0x4）從未到達輪詢點——上游已被映射為
     * 「左」（用戶實測 SELECT 只會左移），永久棄用 SELECT 作觸發鍵；
     * ②兩次 BTOG 的 arg1=0 = 按鍵時不在帶內（停「完成」上按的），
     * 切換無任何可見效果且會在下次進帶時被 CURM 來源判定覆蓋。
     * → 帶外按鍵記 BTOGX（arg1=當前左角X）明確提示無效；
     *   帶內切換 BTOG arg1=切換後模式（1=中 0=退格）。 */
    if ((down & edge) & (KEY_SELECT | KEY_L | KEY_R)) {
        if (gBrOnBS) {
            gBrZhongMode = !gBrZhongMode;
            if (gBrZhongMode) SnapNaming();   /* 中模式需快照供 A 復原 */
            TtyLog("BTOG", gBrZhongMode, gBrPrevX);
        } else {
            TtyLog("BTOGX", gBrLastX9, gBrPrevX);
        }
        return;
    }
    /* v5：中模式下 A = 呼出插件鍵盤（吞掉遊戲原生退格）*/
    if ((down & edge) & KEY_A && gBrOnBS && gBrZhongMode) {
        gRestoreArmed = 1;
        gIconTriggered = true;
        TtyLog("AZH", gSnapLen, gBrPrevX);
        return;
    }
#endif /* !V7_NATIVE_ZHONG */
    if (!gBgcLogged) { LogBgc(); gBgcLogged = true; }
    OamDumpAll();
}
static void PollIconTouch() {
    /* v7.6.24：FC 閘門——僅欄A(gCurFC==0)/欄B(gCurFC==4) 才同步右移。
     * v7.6.23 回歸根因：SyncRamMaps 純 BG 簽名偵測無法區分欄B 與數字
     * (fc=3)/QWERTY(fc=5) 屏（底欄幾何+簽名全同），PollIconTouch 與
     * FlashRenderLog 雙路都把這些屏的底欄誤右移 16px（MSRA(1,11) 日誌
     * 實錘全發生在 fc=3/fc=5 渲染之後）。gCurFC 由 FlashRenderLog 每幀
     * 更新（kbi_rlog.c），0xFFFFFFFF=渲染器未跑過（安全不動）。 */
    if (gCurFC == 0 || gCurFC == 4)
        SyncRamMaps();          /* v7.6.20：遊戲 RAM map buffer 右移+貼中（+16px 根因修復）*/
    UpdateBarB();           /* v7.6.14：欄B（BG1/SBB2）插中態 */
    UpdateModeBarIcon();    /* 欄A（BG2/SBB4）插中態 */
    struct SPITpData raw;
    TPData tp;
    memcpy(&raw, HW_TOUCHPANEL_BUF, sizeof(raw));
    tp.x = raw.x;
    tp.y = raw.y;
    tp.touch = raw.touch;
    tp.validity = raw.validity;
    /* 走原始跳板（不受 TP 過濾鉤子影響），拿到真實校準座標 */
    if (Orig_TPGetCalibratedPoint)
        Orig_TPGetCalibratedPoint(&tp, &tp);
    else
        TP_GetCalibratedPoint(&tp, &tp);
    if (tp.touch) {
        gTouchWasDown = true;
        gTouchLastX = tp.x;
        gTouchLastY = tp.y;
    } else if (gTouchWasDown) {
        gTouchWasDown = false;
        /* 診斷：記錄每次觸摸抬起的座標（用於校準格子位置）*/
        TtyLog("TP", gTouchLastX, gTouchLastY);
        if (gSuppressIconTouch) {
            /* v7.6：OIF 時手指仍按著（enter）→ 這次抬起是插件鍵盤
             * enter 手指的遲到邊沿，座標恰落「中」格吞區 → 吞掉防
             * 重複呼出。只吞一次，之後觸摸恢復正常。 */
            gSuppressIconTouch = 0;
        } else if (gTouchLastX >= ICON_X && gTouchLastX < ICON_X + ICON_W &&
                   gTouchLastY >= ICON_Y && gTouchLastY < ICON_Y + ICON_H) {
            gIconTriggered = true;
        }
    }
}
bool ShouldShowKeyboard() {
    /* v7：郵箱輪詢——遊戲 A/觸摸「中」格 → OvHook_DispatchZhong 寫 1。
     * 放在最前（gBlockModeTouch 未就緒/插件鍵盤已開時也要清郵箱防殘留）*/
    if (gZhongMailbox) {
        gZhongMailbox = 0;
        if (!IsPluginKeyboardVisible()) {
            if (gSuppressIconTouch) {
                /* v7.6：OIF 後殘留郵箱（enter 手指仍按在中格吞區，
                 * Hook_TP 每幀重寫）→ 吞掉，防提交後立即重複呼出 */
                TtyLog("ZMBX-S", 0, 0);
            } else {
                gIconTriggered = true;
                TtyLog("ZMBX", gTouchLastX, gTouchLastY);
            }
        }
    }
    PollIconTouch();
    PollCursorScan();
    if (gIconTriggered) {
        gIconTriggered = false;
        TtyLog("ICO", gTouchLastX, gTouchLastY);
        return true;
    }
    /* v7.7.3：移除 R+X 快捷鍵呼出（用戶定案——遊戲內可視化接口已就緒，
     * 快捷鍵通道退役；中格觸摸/郵箱路徑不變）*/
    return false;
}
