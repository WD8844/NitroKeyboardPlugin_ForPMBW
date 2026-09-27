#include "kbi_internal.h"

/* ============================================================
 * v7：ov194 原生「中」格鉤子（配套 _patch_ov194.py）
 *
 * 逆向結論（2026-09-09，dump_B 離線反彙編）：
 *  - 光標對象：+0xD4=col、+0xD8=row（u32），方法表 +0x10=get-rect
 *    (0x021F27E8)、+0x14=move(0x021F2978)、+0x18=id fn(0x021F2E50)
 *  - 模式欄（row5）內部 col0-6：0-4=五模式格、5=返口、6=完成
 *  - v7 補丁：模式欄擴為 col0-7（5=中、6=返口、7=完成）
 *
 * 接線（全部由 _patch_ov194.py 從 ELF 符號取址後寫入 ov194）：
 *  - ARMv5T Thumb 無寬分支 → ov194 側跳板 = ldr pc,[pc,#imm]
 *    （ARMv5T LDR-PC interwork，字面量 = 鉤子地址|1）
 *  - 本文件鉤子返回 ov194 用 ldr rX,=addr; bx rX（字面量也帶|1）
 *
 * 約定（各鉤子）：
 *  OvHook_IdModeBar    r0=col，返回 id（bx lr 直返 id fn 調用者）
 *  OvHook_SnapEnter    r0=&col(row@+4)、r2=進模式欄記憶；只准動 r1/r3/r12
 *  OvHook_SnapLeave    r0=&col、r2=記憶；只准動 r1/r3/r12
 *  OvHook_RectZhong    r4=out 矩形 {x,y,x2,y2} u32×4（tile 單位）、
 *                      r5=&{col,row}；只准動 r0-r3/r12
 *  OvHook_DispatchZhong r4=obj、r5=event；寫郵箱後回分發器公共出口
 *  OvHook_RLogA/B     （v7.6.15 觀測）閃塊渲染 bl 站點包裝：r0=obj+0x20、
 *                      r1=mode、r2=rect、r3=[obj+0xE6]、棧參=[obj+0xFC]；
 *                      轉儲後轉發原渲染器 0x021F18E4，透傳返回值
 *  （v7.9 FlashColA-D / v8.0 RenderEntry / v8.1 BlockW1-W2 已於 v7.6.1
 *   全部撤銷，回到 v7.6 鉤子集 = 上述 6 個。）
 *  （v7.7 OvHook_EventColFix 已於 v7.8 撤銷：其守衛讀 event+20 而非 row，
 *   且真機實測從未觸發（無 ECF 日誌）；v7.8 的 P_DISP_WIDE_SKIP（跳過
 *   idx7/idx8 填充段）已於 v7.9 撤銷——bad.bin 崩潰真因是 v7.7 跳板自身
 *   覆蓋 3858-385F（r5=event+8 增量被毀）→ get-rect(&event[0]) 垃圾矩形，
 *   原生填充段無辜（v7.6 全程開啟且無崩潰為鐵證）。）
 * ============================================================ */
volatile u32 gZhongMailbox;   /* 遊戲側寫 1（A/觸摸「中」格）→ 插件輪詢清零 */

/* gKbCur = &obj->col（=obj+0xD4，[0]=col [1] =row），由 OvHook_RectZhong /
 * OvHook_DispatchZhong 在遊戲側查詢/事件時捕獲（兩欄的 get-rect arg0 佈局
 * 一致：r5=&{col,row}；Dispatch 的 obj+0xD4 即同址）。
 * v7.6.14：活動欄判定不再依賴 gKbCur（探測實錘：純方向鍵操作時鉤子不觸發、
 * gKbCur 恒 NULL）——改用兩張地圖的原生/貼中特徵直接判定（見 UpdateBarB）。 */
vu32 *gKbCur;

__attribute__((naked, used))
void OvHook_IdModeBar(void) {
    __asm__ volatile(
        "cmp r0, #5\n"
        "blt 1f\n"
        "bne 2f\n"
        "movs r0, #10\n"        /* col5 = 中（死 id10，跳表已重指向） */
        "bx lr\n"
        "2:\n"
        "cmp r0, #7\n"
        "bhi 3f\n"
        "add r0, r0, #1\n"         /* col6→7(返口) col7→8(完成) */
        "bx lr\n"
        "1:\n"
        "add r0, r0, #2\n"         /* col0-4 → 2-6 模式格 */
        "bx lr\n"
        "3:\n"
        "movs r0, #0\n"
        "bx lr\n"
    );
}

__attribute__((naked, used))
void OvHook_SnapEnter(void) {
    __asm__ volatile(
        "ldr r1, [r0, #0]\n"    /* col */
        "cmp r1, #5\n"
        "blt 4f\n"
        "bne 1f\n"
        "movs r3, #5\n"
        "strb r3, [r2, #0]\n"   /* col5=中：記憶 col5 */
        "b 5f\n"
        "1:\n"
        "cmp r1, #8\n"
        "bgt 2f\n"
        "strb r1, [r2, #0]\n"   /* col6-8 → 返口 */
        "movs r1, #6\n"
        "b 5f\n"
        "2:\n"
        "cmp r1, #12\n"
        "bgt 4f\n"
        "strb r1, [r2, #1]\n"   /* col9-12 → 完成 */
        "movs r1, #7\n"
        "5:\n"
        "str r1, [r0, #0]\n"    /* 寫回吸附後 col */
        "4:\n"
        "ldr r3, =0x021F2A77\n" /* 回 move fn 尾（cmp r4,#0） */
        "bx r3\n"
        ".pool\n"
    );
}

__attribute__((naked, used))
void OvHook_SnapLeave(void) {
    __asm__ volatile(
        "ldr r1, [r0, #0]\n"    /* col */
        "sub r1, r1, #5\n"
        "cmp r1, #2\n"
        "bhi 1f\n"              /* 無符號 >2 = col<5 或 >7：不還原 */
        "movs r3, #0\n"
        "cmp r1, #2\n"
        "blt 2f\n"
        "movs r3, #1\n"         /* col7(完成) → 記憶[1] */
        "2:\n"
        "ldrb r1, [r2, r3]\n"
        "str r1, [r0, #0]\n"    /* 還原字符區 col */
        "1:\n"
        "ldr r3, =0x021F2A91\n" /* 回 move fn 出口（pop;bx lr） */
        "bx r3\n"
        ".pool\n"
    );
}

__attribute__((naked, used))
void OvHook_RectZhong(void) {
    /* v7.6.12 col 感知（兩欄共用）：
     *  col5（欄A 中 / 欄B 中）→ {x13,y20,x15,y23}（px104-120，v5.3 真機位）
     *  col6（欄B space，僅欄B col5/6→id10 可達）→ {x15,y20,x29,y23}
     *   （=原生 {x13,x27}+2：鍵體 tx13-26 右移 2 列 → tx15-28（x1 獨佔）。
     *    v7.6.26 定案：鍵整體右移、花紋 c27-31 原生不動；v7.6.25 的
     *    {x15,x27} 配 11 列撕裂案已廢）
     * 返回口按 row 分流：row5 → 欄A get-rect 公共出口 0x021F2885；
     * row3 → 欄B get-rect 出口 0x021F2934（row/col 合法性檢查頭，
     * 尾檢已補 cmp#6）。
     * gKbCur 捕獲：r5=&{col,row}（=obj+0xD4），UpdateModeBarIcon 追蹤用。 */
    __asm__ volatile(
        "ldr r0, =gKbCur\n"
        "str r5, [r0, #0]\n"
        "ldr r0, [r5, #0]\n"    /* col */
        "cmp r0, #6\n"
        "beq 1f\n"
        "movs r0, #13\n"
        "str r0, [r4, #0]\n"
        "movs r0, #20\n"
        "str r0, [r4, #4]\n"
        "movs r0, #15\n"
        "str r0, [r4, #8]\n"
        "movs r0, #23\n"
        "str r0, [r4, #12]\n"
        "b 2f\n"
        "1:\n"
        "movs r0, #15\n"
        "str r0, [r4, #0]\n"
        "movs r0, #20\n"
        "str r0, [r4, #4]\n"
        "movs r0, #29\n"        /* v7.6.26：原生 x1=27 +2（鍵體 tx15-28，x1 獨佔）*/
        "str r0, [r4, #8]\n"
        "movs r0, #23\n"
        "str r0, [r4, #12]\n"
        "2:\n"
        "ldr r0, [r5, #4]\n"    /* row */
        "cmp r0, #5\n"
        "beq 3f\n"
        "ldr r0, =0x021F2935\n" /* 欄B get-rect 出口（合法性檢查頭）|1 */
        "bx r0\n"
        "3:\n"
        "ldr r0, =0x021F2885\n" /* 欄A get-rect 公共真出口 */
        "bx r0\n"
        ".pool\n"
    );
}

/* v7.6.10 定案（2026-09-12）：分發器 idx10 為兩欄共用 id——
 *  - 欄A（平/片/字/數/漢字/中/返口/完成，idfn 0x021F2E50 鏈）：v7 補丁把
 *    col5(中) 映射為原生死 id10；
 *  - 欄B（漢字屏，idfn 0x021F2EC8 + get-rect 0x021F288C）：v7.6.12 起原生
 *    case3 臂改為 col5(中)/col6(space)→id10（原生 col5=space→id10）。
 * v7 起把 idx10 表項改指郵箱臂 → v7.6.10 先以 row 守衛解劫持欄B space；
 * v7.6.12 守衛升級：row==5（欄A 中）或 row==3&&col==5（欄B 中）→ 寫郵箱
 * 回 0x021F3859；其餘（欄B col6=space）→ 直返原生 space 臂 0x021F3806|1
 * （r4/r5 原樣直通，臂尾走 3858 公共出口自會 pop 恢復）。
 * gKbCur 捕獲：r4=obj → obj+0xD4 記入 gKbCur（與 RectZhong 捕獲同址）。 */
__attribute__((naked, used))
void OvHook_DispatchZhong(void) {
    __asm__ volatile(
        "movs r0, #212\n"        /* 0xD4=&col：捕獲 gKbCur 供 UpdateModeBarIcon */
        "add r0, r4\n"
        "ldr r1, =gKbCur\n"
        "str r0, [r1, #0]\n"
        "movs r0, #216\n"        /* 0xD8=row 偏移（imm5 ldr 偏移上限 124，借寄存器）*/
        "ldr r0, [r4, r0]\n"
        "cmp r0, #5\n"
        "beq 2f\n"               /* 欄A row5（中）→ 郵箱 */
        "cmp r0, #3\n"
        "bne 1f\n"               /* 其餘 row → 原生 space 臂 */
        "movs r0, #212\n"
        "ldr r0, [r4, r0]\n"     /* col */
        "cmp r0, #5\n"
        "bne 1f\n"               /* 欄B col6(space) → 原生 space 臂 */
        "2:\n"
        "ldr r0, =gZhongMailbox\n"
        "movs r1, #1\n"
        "str r1, [r0, #0]\n"
        "ldr r0, =" OVA_DISP_EXIT "\n" /* 回分發器公共出口（sp 恢復+重繪） */
        "bx r0\n"
        "1:\n"
        "ldr r0, =0x021F3807\n" /* 原生 idx10 臂（space：E4=9/E0=1），Thumb|1 */
        "bx r0\n"
        ".pool\n"
    );
}

/* v7.9（FlashColA-D 閃塊列校正）、v8.0（RenderEntry 統一攔截）、
 * v8.1（BlockW1/W2 塊寬 bl 鉤子）已於 v7.6.1 全部撤銷——應用戶要求回到
 * v7.6 基線，寬格確認高亮區改由 _patch_ov194.py 的 P_RECT_WIDE1/WIDE2
 * （get-rect 返口/完成臂 x0 各左移 2 tile = 16px）實現。
 * v7.6.2 追加：OvHook_HitRectFix——get-rect 左移同時把觸摸掃描區也帶左了
 * 16px（用戶實測錯誤）。ov194 兩個命中測試（0x021F2548/0x021F25D4 窮舉
 * col/row → thunk 0x021F2704 → get-rect → ×8 → 點包含測試）的 bl 調用點
 * 被重定向到此包裝：調完原 get-rect 後，row==5 且 col∈{6,7}（返口/完成）
 * 時 x0/x1 各 +2 tile，掃描區精確還原 v7.6 原生。高亮路徑（分發器 3858、
 * 每幀重放 31DA、鍵處理器等共 11 個 thunk 調用方）不經此包裝，高亮不動。 */
u32 OvHook_HitRectFix(void *obj, u32 *self, u32 *rect) {
    typedef u32 (*RectFn)(u32 *self, u32 *rect);
    RectFn fn = (RectFn) ((void **)obj)[4];    /* obj+0x10 = get-rect 虛方法 */
    u32 r = fn(self, rect);
    if (self[1] == 5 && (self[0] == 6 || self[0] == 7)) {
        rect[0] += 2;   /* x0：返口 104→120px、完成 168→184px（v7.6 原生）*/
        rect[2] += 2;   /* x1：寬度 8 tile 不變 */
    }
    return r;
}
