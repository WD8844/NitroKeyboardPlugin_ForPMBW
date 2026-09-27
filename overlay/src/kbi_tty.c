#include "kbi_internal.h"

/* ============================================================
 * no$gba / melonDS TTY 調試輸出
 * 逐字節寫 0x04FFFA1C，寫 0 結束一條消息（no$gba debug message 格式）
 * ============================================================ */
/* ============================================================
 * no$gba / melonDS TTY 調試輸出
 * 逐字節寫 0x04FFFA1C，寫 0 結束一條消息（no$gba debug message 格式）
 * ============================================================ */
static void TtyStr(const char *s) {
    while (*s) { *(vu8 *)0x04FFFA1C = *s++; }
    *(vu8 *)0x04FFFA1C = 0;
}
static void TtyHex(u32 v) {
    static const char hx[16] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
    char buf[9];
    for (int i = 0; i < 8; i++) buf[i] = hx[(v >> (28 - i * 4)) & 0xF];
    buf[8] = 0;
    TtyStr(buf);
}
void TtyLog(const char *tag, u32 a, u32 b) {
    TtyStr("["); TtyStr(tag); TtyStr(":"); TtyHex(a); TtyStr(","); TtyHex(b); TtyStr("]");
}
