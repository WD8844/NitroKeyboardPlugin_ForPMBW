#ifndef GAME_VARIANT_H
#define GAME_VARIANT_H

/* ============================================================
 * 黑白雙版地址變體定案（2026-09-13 白版偵察，_white_recon2b/2c/3b/4/5）
 *
 * 規律實錘：
 *   - arm9 代碼全域 Δ=+0x18（19/21 符號 24B 特徵唯一命中）
 *   - ov 區全域 Δ=+0x20（含 ov194 全部 32 個補丁站點原生字節一致）
 *   - arm9 內指向 ov 區數據的變量 Δ=+0x20（堆描述符表基 20/20、
 *     fill entry 表基 46/46 引用字面量全體一致）
 *   - secure area 黑白佈局不同：白版 swi5 stub @0x02004422（唯一命中）；
 *     注入點 0x02004450 白版仍為純垃圾（白版真代碼 @0x02004600，不衝突）
 *
 * GAME_WHITE 由 common/common.mk 的 GAME_VARIANT=WHITE 注入 -D。
 * 默認（無 GAME_WHITE）= 黑版 v7.7.3 定案值，字節級不變。
 * ============================================================ */

#ifdef GAME_WHITE
/* ---- 精灵宝可梦 白_ver.2.1.8 ---- */
#define ADDR_FND_ALLOC       0x02060BC4u  /* FndAllocFromExpHeapEx (+0x18) */
#define ADDR_FND_FREE        0x02060D54u  /* FndFreeToExpHeap (+0x18) */
#define ADDR_HEAP_DESC_BASE  0x02146714u  /* [x]=堆描述符數組基址 (+0x20) */
#define ADDR_FILL_ENTRY_TBL  0x02146804u  /* [x]=fill entry 表基 (+0x20) */
#define ADDR_TP_CALIB        0x0208B028u  /* TP_GetCalibratedPoint (+0x18) */
#define ADDR_FILL_FN         0x02041220u  /* fill (+0x18，診斷轉儲用) */
#define ADDR_HELPER2         0x02040D50u  /* helper2 (+0x18，診斷轉儲用) */
#define ADDR_HELPER1         0x02042144u  /* helper1 (+0x18，診斷轉儲用) */
#define ADDR_OV194_RENDERER  0x021F1905u  /* ov194 渲染器 Thumb 入口|1 (+0x20) */
/* ov194 內返回口（kbi_ovhook.c 內聯匯編字面量，全部 +0x20） */
#define OVA_MOVEN_TAIL  "0x021F2A97"  /* 回 move fn 尾（cmp r4,#0） */
#define OVA_MOVEN_EXIT  "0x021F2AB1"  /* 回 move fn 出口（pop;bx lr） */
#define OVA_RECTB_EXIT  "0x021F2955"  /* 欄B get-rect 出口（合法性檢查頭）|1 */
#define OVA_RECTA_EXIT  "0x021F28A5"  /* 欄A get-rect 公共真出口 */
#define OVA_DISP_EXIT   "0x021F3879"  /* 分發器公共出口（sp 恢復+重繪） */
#define OVA_SPACE_ARM   "0x021F3827"  /* 原生 idx10 臂（E4=9/E0=1）|1 */
#else
/* ---- 精灵宝可梦 黑_ver.2.1.8（v7.7.3 定案）---- */
#define ADDR_FND_ALLOC       0x02060BACu
#define ADDR_FND_FREE        0x02060D3Cu
#define ADDR_HEAP_DESC_BASE  0x021466F4u
#define ADDR_FILL_ENTRY_TBL  0x021467E4u
#define ADDR_TP_CALIB        0x0208B010u
#define ADDR_FILL_FN         0x02041208u
#define ADDR_HELPER2         0x02040D38u
#define ADDR_HELPER1         0x0204212Cu
#define ADDR_OV194_RENDERER  0x021F18E5u
#define OVA_MOVEN_TAIL  "0x021F2A77"
#define OVA_MOVEN_EXIT  "0x021F2A91"
#define OVA_RECTB_EXIT  "0x021F2935"
#define OVA_RECTA_EXIT  "0x021F2885"
#define OVA_DISP_EXIT   "0x021F3859"
#define OVA_SPACE_ARM   "0x021F3807"
#endif

#endif /* GAME_VARIANT_H */
