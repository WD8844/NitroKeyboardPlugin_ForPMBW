#include "kbi_internal.h"

/* ============================================================
 * 觸摸過濾鉤子 + overlay 載入初始化
 * ============================================================ */
/* ============================================================
 * 觸摸過濾鉤子：模式欄「中」格與遊戲「返口」觸摸判定區重疊的補償
 *
 * 鉤住 arm9 的 TP_GetCalibratedPoint(0x0208B010)（ARM 函數）：
 *   - 「中」格區域 (x104-119, y160-183) → 吞掉 touch + 置郵箱
 *     （遊戲看不到，不會誤觸返口退格；插件自己的 PollIconTouch
 *     走原始跳板拿真座標）
 *   - 返口/完成區域 → v7.2 起直通（get-rect 修復後遊戲矩形迭代
 *     觸摸路徑自行命中 col6/col7）
 * 插件鍵盤打開時（遊戲主循環已掛起、kmod 自己也走此函數讀觸摸）→ 直通
 * ============================================================ */
void (*Orig_TPGetCalibratedPoint)(TPData *disp, const TPData *raw);
vu8 gBlockModeTouch;   /* 模式欄已右移+新格已貼上 = 1（UpdateModeBarIcon 維護）*/
vu8 gSuppressIconTouch; /* v7.6：OIF(提交/取消)時觸摸仍按下=1 → 吞掉接下來
                                * 的一次抬起邊沿（enter 手指抬起座標恰落「中」格
                                * 吞區 104-119x160-183 → 否則 ICO 重複呼出）*/


/* ARM 函數：I-cache 全量無效化（Thumb 模式無 MCR，須 ARM 態執行）*/
__asm__(
".arm\n"
".global IC_InvalidateAll_ARM\n"
".type IC_InvalidateAll_ARM, %function\n"
"IC_InvalidateAll_ARM:\n"
"   mcr p15, 0, r0, c7, c5, 0\n"
"   bx lr\n");
void IC_InvalidateAll_ARM(void);

static void Hook_TPGetCalibratedPoint(TPData *disp, const TPData *raw) {
    if (Orig_TPGetCalibratedPoint) Orig_TPGetCalibratedPoint(disp, raw);
    if (!gBlockModeTouch || IsPluginKeyboardVisible() || !disp->touch) return;
    if (disp->y < 160 || disp->y >= 184 || disp->x < 104) return;
    if (disp->x < 120) {
        disp->touch = 0;                 /* 「中」格：呼出插件鍵盤（v7）*/
        gZhongMailbox = 1;
        return;
    }
    /* v7.2：返口/完成觸摸直通（無條件——原 #if V7_NATIVE_ZHONG 在此處
     * 位於 #define 之前=未定義=0，一直走 x-16 舊補償分支，已廢除）。
     * 遊戲觸摸→格轉換 0x021F2548 是矩形迭代式（逐格調 get-rect 做點
     * 命中測試），get-rect 尾檢查 cmp#6→#7 修復後 col6(返口,E4=5)/
     * col7(完成,E4=7) 矩形正確 → 遊戲自己處理即可。 */
}

static void InstallTouchFilter(void) {
    static HookARMEntry entry;
    static bool installed;
    if (installed) return;
    if ((*(vu32 *)ADDR_TP_CALIB & 0xFFFF0000) != 0xE92D0000) {
        TtyLog("TPHK-NO", *(vu32 *)ADDR_TP_CALIB, 0);  /* 首指令非 push，放棄 */
        return;
    }
    entry.functionAddr   = (void *)ADDR_TP_CALIB;
    entry.origFunctionRef = (void **)&Orig_TPGetCalibratedPoint;
    entry.hookFunction   = Hook_TPGetCalibratedPoint;
    HookFunction(&entry);
    installed = true;
    /* HookFunction 只刷 D-cache，補一條 I-cache 全量無效化 */
    IC_InvalidateAll_ARM();
    TtyLog("TPHK-OK", ADDR_TP_CALIB, 0);
}

void OnOverlayLoaded() {
    static bool init = false;
    if (init) return;
    init = true;
    InstallTouchFilter();
}
