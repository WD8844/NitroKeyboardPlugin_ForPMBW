/* 白版（精灵宝可梦 白_ver.2.1.8）符號地址定案
 * 偵察依據（2026-09-13 _white_recon2b/2c/3b/4/5）：
 *  - arm9 代碼全域 Δ=+0x18（19/21 符號 24B 特徵唯一命中；FS_GetLength/
 *    FndAllocFromExpHeapEx 多命中由佈局序關係唯一確定）
 *  - arm9 數據/變量全域 Δ=+0x18（PtrToArenaLo 變量值 0x02103B80→0x02103BA0
 *    為其指向的 ov 區對象，隨 ov 區 +0x20）
 *  - ov 區全域 Δ=+0x20；arm9 內指向 ov 區數據的變量（0x021466F4→0x02146714
 *    堆描述符表基 20/20 引用一致、0x021467E4→0x02146804 entry 表基 46/46）
 *  - secure area（0x02004000-0x02004800）內容黑白不同：白版 swi5 stub
 *    （002205df 7047）唯一命中 @0x02004422 = SVC_WaitVBlankIntr；
 *    注入點 0x02004450 仍為純垃圾（白版真代碼 @0x02004600，不衝突）
 *  - ov10 白 ram=0x02154E80（+0x20）→ 靜態初始化表 0x02172404/E8
 */
FS_InitFile = 0x020787D0;
FS_OpenFile = 0x02078B3C;
FS_ReadFile = 0x02078A94;
FS_SeekFile = 0x02078A68;
FS_GetLength = 0x020789B8;
FS_CloseFile = 0x020789A4;
FS_LoadOverlay = 0x02079188;
FS_LoadOverlayInfo = 0x02078C58;
FS_LoadOverlayImage = 0x02078DD4;
FS_StartOverlay = 0x02078F50;
OS_WakeupThreadDirect = 0x0208578C;
OS_SleepThread = 0x020856D4;
TP_GetCalibratedPoint = 0x0208B028;
OS_AllocFromHeap = 0x02086804;
OS_FreeToHeap = 0x0208690C;
FndAllocFromExpHeapEx = 0x02060BC4;
FndFreeToExpHeap = 0x02060D54;
OS_SaveContext = 0x02085B2C;
SVC_WaitVBlankIntr = 0x02004422|1;
OS_CreateThread = 0x020852E4;
LanucherThreadContext = 0x02150E7C;
OSi_IrqThreadQueue = 0x02FE00A0;
HW_BUTTON_XY_BUF = 0x02FFFFA8;
HW_TOUCHPANEL_BUF = 0x02FFFFAA;
PtrToArenaLo_twl = 0x02086664;
PtrToArenaLo = 0x02086668;

Orig_OverlayStaticInitBegin = 0x02172404;
Orig_OverlayStaticInitEnd = 0x02172408;
