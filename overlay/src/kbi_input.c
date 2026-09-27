#include "kbi_internal.h"

/* ============================================================
 * 輸入寫回：BW 命名界面上下文（從「命名界面輸入 Aa0y4／ＡＢ」快照逆向）
 * - 命名界面打開時遊戲建立子堆（EXPH），地址會話間基本穩定但仍動態掃描
 * - 上下文佈局（相對子堆 EXPH 結構基址）：
 *   +0xCC = 輸入緩衝（全角 UTF-16 u16 × 12）
 *   +0xEA = 當前長度 u16
 *   +0xEE = 容量 u16（=5，BW 名字上限；只讀不寫！）
 *   緩衝[已輸入長度] = 0xFFFF（動態終止）
 *   注意：buf[11]=FFFF 是舊會話殘留數據，不是初始化特徵（全新上下文為 0）！
 * 定位特徵（ＡＢ 快照全 10 個 EXPH 驗證，唯一命中零誤報）：
 *   EXPH 頭 + 1<=容量<=16 + 長度<=容量 + buf[長度]==0xFFFF
 * ============================================================ */

/* 遊戲字符碼合法性（全角/ASCII/CJK/標點/遊戲符號鍵盤）
 * v7.6.11：數字符號鍵盤輸入的真實 Unicode 碼（bad.bin 實錘：0x2660 ♠類、
 * 0x00D7 ×、0xFF08 （）原本不在區間 → loose 校驗否決唯一 EXPH 候選 →
 * OIF-NOCTX 丟提交。補：Latin-1 補充（×÷±§）、0x2010-0x27BF（標點/
 * 箭頭/圈數字/幾何圖形/雜項符號/貨幣，含 ♪☆★♂♀※…）、全角區）*/
static bool IsValidGameChar(u16 c) {
    return (c >= 0x20 && c <= 0x7E) || (c >= 0x00A1 && c <= 0x00FF) ||
           (c >= 0x2010 && c <= 0x27BF) || (c >= 0x3000 && c <= 0x30FF) ||
           (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xFF01 && c <= 0xFF5E) ||
           (c >= 0xFF10 && c <= 0xFF19);
}

/* 掃描 EWRAM 中的 EXPH + 命名輸入框特徵，返回 EXPH 基址（未找到返回 0）*/
u32 FindNamingHeap(void) {
    u32 strict = 0, loose = 0;
    u32 exCount = 0, firstEx = 0;
    for (u32 addr = 0x02000000; addr + 0xF0 <= 0x02400000; addr += 4) {
        if (*(vu32 *)addr != FND_HEAP_MAGIC) continue;  /* 'HPXE' */
        u16 *buf = (u16 *)(addr + NAMING_BUF_OFFSET);
        u16 len = *(u16 *)(addr + NAMING_LEN_OFFSET);
        u16 cap = *(u16 *)(addr + NAMING_CAP_OFFSET);
        if (cap < 1 || cap > NAMING_MAX_SLOTS || len > cap) continue;
        if (buf[len] != 0xFFFF) continue;      /* 動態終止符 */
        exCount++;
        if (!firstEx) firstEx = addr;
        TtyLog("EX", addr, len | (cap << 16));
        /* 嚴格：空輸入框（全新上下文 buf 全 0，刪空後 buf[0]=FFFF，都算）*/
        if (len == 0 && (buf[0] == 0x0000 || buf[0] == 0xFFFF)) {
            strict = addr;  /* 取最後一個匹配 */
        } else if (len > 0) {
            bool ok = true;
            u16 bad = 0;
            for (int k = 0; k < len; k++) {
                if (!IsValidGameChar(buf[k])) { ok = false; bad = buf[k]; break; }
            }
            if (ok) loose = addr;
            else TtyLog("EX-BAD", addr, bad);  /* 記錄首個被否決的字符碼 */
        }
    }
    TtyLog("NSCAN", exCount, firstEx);
    if (strict) { TtyLog("NCTX", strict, 0); return strict; }
    if (loose) { TtyLog("NCTX-L", loose, 0); return loose; }
    /* v7.6.11 兜底：字符校驗全滅但全 4MB 掃描僅此一個 EXPH 候選
     * （cap/len/buf[len]==FFFF 硬特徵已全過）→ 接受唯一候選 */
    if (exCount == 1) { TtyLog("NCTX-U", firstEx, 0); return firstEx; }
    return 0;
}

int GetMaxInputLength() {
    u32 heap = FindNamingHeap();
    if (heap) {
        u16 cap = *(u16 *)(heap + NAMING_CAP_OFFSET);
        if (cap >= 1 && cap <= NAMING_MAX_SLOTS) {
            /* 增量輸入：返回剩餘空間（容量-當前已輸入長度）*/
            u16 cur = *(u16 *)(heap + NAMING_LEN_OFFSET);
            int remain = (int)cap - (int)cur;
            return remain > 0 ? remain : 0;
        }
    }
    return 5;  /* BW 命名標準上限 */
}

void OnInputFinished(u16 *inputText, int length, bool isCanceled) {
    TtyLog("OIF", length, isCanceled);
    /* v7.6 防重複呼出：OIF 時若手指仍按在屏上（觸摸 enter 提交的典型
     * 情形——gTouchWasDown 從「中」格呼出起一直保持 1），其抬起邊沿
     * 座標 (111,173) 恰落「中」格吞區 → 會經 TP/ICO 路徑再呼出一次。
     * 置 suppress 吞掉下一次抬起邊沿並清殘留郵箱。A 鍵呼出無觸摸按下，
     * gTouchWasDown=0，不置位（不影響後續正常觸摸）。 */
    if (gTouchWasDown) {
        gSuppressIconTouch = 1;
        gZhongMailbox = 0;
        TtyLog("OIF-S", 0, 0);
    }
    /* FinalizeKeyboard 調完本函數立即銷毀模組，之後不再取字模。
     * 在此釋放 256KB 字庫緩衝：否則遊戲下一場景大分配失敗 → 黑屏卡死 */
    ReleaseFont();
    /* v5：A 呼出「中」時遊戲可能同幀已執行退格刪字——取消鍵盤則復原快照 */
    if (gRestoreArmed) {
        if (isCanceled) RestoreNamingSnap();
        else gRestoreArmed = 0;
    }
    if (isCanceled || length <= 0) return;

    u32 heap = FindNamingHeap();
    if (!heap) {
        TtyLog("OIF-NOCTX", 0, 0);
        return;
    }
    u16 cap = *(u16 *)(heap + NAMING_CAP_OFFSET);
    if (cap < 1 || cap > NAMING_MAX_SLOTS) cap = 5;

    /* 增量輸入：從上下文當前長度處追加（配合遊戲鍵盤/多次插件輸入組合名字）*/
    int start = *(u16 *)(heap + NAMING_LEN_OFFSET);
    if (start < 0 || start > cap) start = cap;
    int n = length;
    if (start + n > cap) n = cap - start;  /* 超容量自動截斷 */

    u16 *buf = (u16 *)(heap + NAMING_BUF_OFFSET);
    for (int i = 0; i < n; i++) {
        u16 c = inputText[i];
        if (c >= 0x20 && c <= 0x7E) {
            c = HalfToFullWidth(c);  /* 半角→全角（遊戲緩衝存全角）*/
        }
        buf[start + i] = c;
    }
    /* 清空新終止符之後的槽位（殘留數據會干擾遊戲解析）*/
    for (int i = start + n + 1; i < NAMING_MAX_SLOTS; i++) {
        if (buf[i] != 0xFFFF) buf[i] = 0x0000;
    }
    buf[start + n] = 0xFFFF;
    *(u16 *)(heap + NAMING_LEN_OFFSET) = (u16)(start + n);
    /* 觸發遊戲重繪輸入框（逆向確認：0x021F206C 追加字符函數寫 buf+0x28=1，
     * 遊戲主循環檢測到髒標誌後調用 overlay 渲染函數重繪並清零該標誌）*/
    *(vu32 *)(heap + NAMING_BUF_OFFSET + 0x28) = 1;
    /* 注意：+0xEE 是容量字段，絕不能寫！ */
    TtyLog("OIF-W", heap, start + n);
    TtyLog("OIF-A", start, n);  /* 追加位置和實際追加數 */
    /* 回讀校驗：確認寫入真的落在內存（排除緩存/地址問題）*/
    TtyLog("OVF", buf[0], *(u16 *)(heap + NAMING_LEN_OFFSET));
}
