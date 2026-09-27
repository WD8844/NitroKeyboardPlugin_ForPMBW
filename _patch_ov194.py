#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
v7: ov194 原生「中」格補丁器（在 patch.py 之後、ndstool 之前運行）

逆向依據（2026-09-09 dump_B 離線反彙編，運行鏡像與 ROM 解壓流逐字節一致）：
  - ov194 ram A(0x021F1320)-A(0x021F5620) (0x4300)，flags=3（SDK 代碼壓縮流）
  - move fn A(0x021F2978)：模式欄 col 迴圈 #6→#7
  - get-rect A(0x021F27E8)：跳表@A(0x021F2806)（base A(0x021F2808)）
  - 分發器 A(0x021F37A0)：跳表@A(0x021F37D0)（base A(0x021F37D2)）
  - id fn A(0x021F2E50)：模式欄分支 @A(0x021F2E94)-A(0x021F2EA1)

補丁策略：所有新邏輯放在插件側 OvHook_* 鉤子（ELF 符號取址），
ov194 側只放跳板。⚠️ Thumb16 無法編碼 ldr pc（LDR literal 的 Rt 只有
3 位），0x4800|(15<<8)=0x4F00 會解碼成 ldr r7 —— v7.0 真機三故障根因。
正確跳板 = ldr r3,[pc,#imm] + bx r3 + 4B 對齊字面量：
  at%4==0: 8B  [4B00][4718][lit@at+4]
  at%4==2: 10B [4B01][4718][nop][lit@at+6]（多覆蓋 2B，須被鉤子重實現）
r3 在五個落點均為死寄存器（move fn 尾段 r3 僅由 pop 恢復；id fn 按
AAPCS r1-r3 caller-saved；jump-table 臂的公共出口 2884/3858 不用 r3）。

模式欄新語義：col0-4=五模式格(id2-6)、col5=中(id10)、col6=返口(id7)、
col7=完成(id8)、col≥8 無效(id0)。寬鍵矩形臂（get-rect）不補——貼圖右移
（插件 UpdateModeBarIcon）與公式隨 col+1 右移量一致。
分發器真表（v7.5 修正解碼，base=A(0x021F37D0)）：idx7→E4=5 退格、
idx8→E4=7 完成、idx9→SE0x54c+E4=8、idx10→E4=9（改指郵箱臂=中）。
中 = tile 13-15 = 括號 100/116（v5.3 真機驗證位置）。

v7.6.9 定案（2026-09-12，v7.6.6/7/8 三連真機否決後）：ov194 補丁集 =
v7.6.5 原樣（四個畫點站點全原生、RectZhong 跳板回 A(0x021F5604)、3838 死口袋
原生殘骸），ov237/kmod = v7.6.5 字節級（kmod=dd48f46e）。寬格 +16px 修正
懸置——v7.6.8 cave 三重編碼錯誤（0xD004 目標錯 1、0xD000 是 BEQ 非 BNE、
0x1E49=subs #1 非 #2）致小格確認閃塊全錯誤左移 8px、寬格未校正，真機實錘
後全撤。當前交付 = fallback 鏡像 5e9fb9da055174667e8d3c0474d53315（用戶
實測：除寬格確認閃塊 +16px 外全部正常）。+16px 下一步 = pal3/pal4 站點
無門控全參數日誌定位真實坐標後再定校正規則。

v7.6.10 定案（2026-09-12，用戶真機實錘「欄B 選中 space 彈出插件鍵盤」）：
ov194 存在兩套鍵盤物件路徑——欄A（模式欄=平/片/字/數/漢字/中/返口/完成，
idfn A(0x021F2E50) + get-rect A(0x021F27E8)，v7 補丁對象）與欄B（漢字屏
平/片/字/數/漢字/space，idfn A(0x021F2EC8) + get-rect A(0x021F288C)，原生未動；
模式欄=row3，space=row3col5 原生 id10，寬格矩形臂 A(0x021F291E) x13-27）。
兩欄共享分發器 A(0x021F37A0)，v7 的兩處補丁誤傷欄B：
  ①idx10 表項 @A(0x021F37E4) →郵箱臂：欄B space（id10）被劫持（真機實錘）；
  ②郵箱跳板體壓在 A(0x021F3830) = 原生 idx9 臂前 8B：欄B row2 兩端鍵（id9）
    事件落入跳板同樣被劫持。
修法：跳板遷至 A(0x021F560C)（TRAMP_RECT 後半口袋）、A(0x021F3830)-37 恢復原生、
OvHook_DispatchZhong 加 row 守衛（r4=obj，[obj+0xD8]==5 → 郵箱回 A(0x021F3859)；
其餘 → 直返原生 space 臂 A(0x021F3806)|1，r4/r5 直通臂尾 3858 公共出口）。
欄A 中行為零變化；欄B 完全還原原生。欄B 插「中」為下一輪獨立特性
（需 idfn 2EC8 row3 col5/col6 分派 + 288C idx10 臂 col 感知矩形 +
UpdateModeBarIcon 第二佈局狀態 + move fn row3 迴圈邊界查證）。

v7.6.12 定案（2026-09-12 晚，欄B 插「中」）：用戶拍板把「中」插入欄B
（漢字屏「平/片/字/數/漢字/space」→「平/片/字/數/漢字/中/space」）。
欄B 逆向定案：idfn A(0x021F2EC8) case3 臂（A(0x021F2F20)，10B）col==5→id10/
else col+2；get-rect A(0x021F288C) id10 臂（A(0x021F291E)，22B）= space
{x13,y20,x27,y23}、尾檢 A(0x021F295E) cmp#5；move fn row3 case（0x021F2D90）
X-wrap #5 兩處（2D9A/2D9E），row3→row0/2 下行子跳表帶 bhi 越界守衛
（col>5 保持原 col 彈出，row0/2 均 10 列合法）→ 無需動子跳表。
五處補丁（全 2B 半字原位 / 10B 跳板，零口袋需求）：
  ①idfn case3 原位 10B 換 10B：col0-4→id2-6 不變、col5/6→id10
    （6800 1C83 2B06 D900 230A：col+2≤6 即返回，否則 id10）；
  ②get-rect B 尾檢 cmp#5→#6（col6=space 合法化）；
  ③get-rect B id10 臂 → 內嵌 10B 跳板 → OvHook_RectZhong（col 感知：
    col5=中 {13,20,15,23}、col6=space {15,20,29,23}；按 row 選返回口：
    row5→A(0x021F2885)（欄A 公共出口）、row3→A(0x021F2934)（欄B 出口））；
  ④move fn row3 X-wrap #5→#6 ×2（col6=space 可達）；
  ⑤分發器 idx10 表項照舊 → OvHook_DispatchZhong 守衛升級：
    row==5 或 (row==3 && col==5) → 郵箱；其餘 → 原生 space 臂。
插件側：RectZhong/DispatchZhong 捕獲 gKbCur=&obj->col（row@[+4]），
UpdateModeBarIcon 逐幀追蹤 gActiveBar（row3=欄B/row5=欄A），欄B 態
快照空間鍵 tx13-27 → 右移貼中（MBRB 日誌）。欄A 兩態特徵優先判定，
天然互斥防 stale 指針誤傷。
"""
import os
import re
import struct
import subprocess
import sys

# 依赖 ndspy（pip install -r script/requirements.txt）
import ndspy.codeCompression as CC

# ---- 黑白雙版變體（2026-09-13 白版移植定案）----
# 白版：ov 區全域 Δ=+0x20（arm9 Δ=+0x18 與本補丁器無關）。
# A(addr) 為 ov194 絕對地址統一偏移；NATIVE 表為「保持原生」斷言的
# per-variant 半字值（白版值由 _white_recon6.py 從白版原生鏡像提取：
# movw/movt 載 fill 的站點隨 fill+0x18 系統性 -4，idx9 臂 bl 後半
# FCD7→FCC7，其餘與黑版一致）。
def _read_variant():
    import io
    try:
        for _l in io.open('common/config.mk', encoding='utf-8'):
            _m = re.match(r'GAME_VARIANT\s*:=\s*(\S+)', _l.strip())
            if _m:
                return _m.group(1)
    except OSError:
        pass
    return 'BLACK'

GAME_VARIANT = _read_variant()
DELTA = 0x20 if GAME_VARIANT == 'WHITE' else 0


def A(addr):
    """ov194 絕對地址 → 當前變體地址"""
    return addr + DELTA


OV_FILE_DIR = 'rom_white' if GAME_VARIANT == 'WHITE' else 'rom'
OV_ROM_IMG  = '_ov194_rom_%s.bin' % GAME_VARIANT.lower()

# 「保持原生」斷言位點 → (黑版值, 白版值) 半字元組
NATIVE_ASSERTS = {
    0x021F197A: ((0xF64F, 0xEC46), (0xF64F, 0xEC42)),  # blx→fill（main 中斷言）
    0x021F1990: ((0xF64F, 0xEC3A), (0xF64F, 0xEC36)),  # blx→fill
    0x021F19D6: ((0xF64F, 0xEC18), (0xF64F, 0xEC14)),
    0x021F1A00: ((0xF64F, 0xEC02), (0xF64F, 0xEBFE)),
    0x021F1900: ((0x1C28, 0x1C14, 0x9908, 0x30A0),) * 2,
    0x021F1978: ((0x2302, 0xF64F, 0xEC46, 0x2003), (0x2302, 0xF64F, 0xEC42, 0x2003)),
    0x021F198E: ((0x2302, 0xF64F, 0xEC3A, 0x2C00), (0x2302, 0xF64F, 0xEC36, 0x2C00)),
    0x021F3830: ((0x4816, 0xF612, 0xFCD7, 0x1C20), (0x4816, 0xF612, 0xFCC7, 0x1C20)),
    0x021F3838: ((0x2108, 0x30E4, 0x6001, 0x31F8),) * 2,
    0x021F2570: ((0xF000, 0xF8C8),) * 2,   # 命中測試 bl→thunk（同 overlay 相對，不變）
    0x021F261A: ((0xF000, 0xF873),) * 2,
    0x021F382A: ((0xE012,),) * 2,
    0x021F382E: ((0xE010,),) * 2,
    0x021F3858: ((0x3508,),) * 2,
    0x021F284A: ((0x1CC2,),) * 2,
    0x021F2868: ((0x320B,),) * 2,
    0x021F1968: ((0x1CC7,),) * 2,
}


def nat(at):
    """當前變體的「保持原生」斷言值（at 可為已偏移地址，鍵為黑版原生地址）"""
    pair = NATIVE_ASSERTS[at - DELTA]
    return pair[1] if GAME_VARIANT == 'WHITE' else pair[0]

BASE     = 0x021F1320 + DELTA
ELF      = 'overlay_0237.elf'
OV_FILE  = os.path.join(OV_FILE_DIR, 'overlay', 'overlay_0194.bin')
TBL_FILE = os.path.join(OV_FILE_DIR, 'overlay_table.bin')
OV_ID    = 194

OBJDUMP  = r'D:/devkitpro/devkitARM/bin/arm-none-eabi-nm.exe'

# ov194 補丁點（RAM 地址）
P_MOVE_WRAP_MOVS = A(0x021F29FE)   # movs r5,#6 → #7
P_MOVE_WRAP_CMP  = A(0x021F2A02)   # cmp  r5,#6 → #7
P_RECT_TAIL_CMP  = A(0x021F287A)   # get-rect 尾檢查 cmp r0,#6 → #7（col7=完成否則被殺）
# v7.6.1 寬格臂左移（P_RECT_WIDE1/WIDE2 寫入 #1/#9）與 v7.6.2 命中測試 bl
# 重定向（P_HIT_BL1/BL2 → OvHook_HitRectFix）均已於 v7.6.3 撤銷：用戶實測
# get-rect 左移把光標括號框/pal3 確認閃光/觸摸掃描區整體帶左 16px（三者同源
# get-rect），HitRectFix 只矯正了命中測試一個消費者，光標框仍偏。用戶裁決：
# 「光標框掃描區恢復成 v7.6 的樣子，不需要做其他更動」→ ov194 寬格處理
# 100% 原生（返口 120-184px、完成 184-248px）。用戶滿意的高亮（懸停 pal4
# 閃塊）由 [obj+0xFC] 驅動，不經 get-rect，零影響。
# 歷史地址（僅存檔）：P_RECT_WIDE1=0x021F284A(1CC2) P_RECT_WIDE2=0x021F2868(320B)
#                    P_HIT_BL1=0x021F2570 P_HIT_BL2=0x021F261A（bl 021F2704）
# v7.6.4 → v7.6.5 撤銷定案：row5 高亮塊 X 校正（#3→#1）已撤。
#   v7.6.4a 真機實測（用戶回報）：前5模式格高亮錯誤左移 16px——0x021F1968
#   的 adds r7,r0,#3 服務 row5 全部格（前5模式格 FC=0-4 → X=3/5/7/9/11
#   本來就正確；#1 把它們全帶左 2 tile），而返口/完成 +16px 的真根因在
#   貼圖層（MBRU 快照重建判據被 pal4 指示塊擊敗，見插件 C 源 v7.6.5），
#   X 校正治不了 → 兩頭落空。0x021F1968 保持原生 0x1CC7。
#   ⚠️ 編碼事故存檔（2026-09-11 bad.bin "Bad CPSR" 定案）：
#   Thumb16 ADDS imm3 編碼 0001110 iii Rnn Rddd：imm3 在 bits[8:6]、
#   Rd 在 bits[2:0]。0x1CC5 = adds r5,r0,#3（非 adds r7,r0,#1！），
#   把渲染器對象指針 r5 覆蓋成 X 座標 → stmia/strh 寫進 0x0000009x
#   BIOS 區 → blx 野跳轉 → Bad CPSR 進鍵盤即死。
#   正確編碼：adds r7,r0,#1 = 0001110 001 000 111 = 0x1C47。
P_FLASH_X        = A(0x021F1968)   # 原生 adds r7,r0,#3(1CC7)，v7.6.5 起只斷言不寫
TRAMP_SNAP_IN    = A(0x021F2A58)   # 進模式欄吸附塊頭（原 30B 塊，只佔前 8B）
TRAMP_SNAP_OUT   = A(0x021F2A7A)   # 出模式欄還原塊頭（原 22B 塊）
TRAMP_IDMODE     = A(0x021F2E96)   # id fn 模式欄分支（保留 2E94 ldr r0,[r0]）
TRAMP_IDMODE2    = A(0x021F2EBA)   # ★第二份 id 重映射（fn 2EA4，type2/3 鍵盤物件用）
TRAMP_RECT       = A(0x021F5604)   # 尾部零填充口袋（28B 可用）；
                                # 3838 死口袋保持原生（v7.6.8 曾遷入後否決回滾）
TRAMP_DISPATCH   = A(0x021F560C)   # v7.6.10：郵箱跳板遷至此（原 0x021F3830 正壓
                                # 在原生 idx9 臂前 8B 上——欄B row2 兩端鍵=id9
                                # 事件被劫持進郵箱。0x021F3830-37 恢復原生
                                # 4816/F612/FCD7/1C20，idx9 臂還活著）。
                                # idx10 表項 base=0x021F37D2，偏移 0x1E3A 合法。
# v7.9（FlashCol×4 跳板）、v8.0（TRAMP_RENDER=0x021F1900 RenderEntry）、
# v8.1（TRAMP_BLOCKW1/2=0x021F1978/198E bl 鉤子）已於 v7.6.1 全部撤銷。

# ---- v7.6.12：欄B（漢字屏）插「中」補丁點 ----
P_RECTB_TAIL_CMP  = A(0x021F295E)   # get-rect B 尾檢查 cmp r0,#5 → #6（col6=space 合法化）
P_RECTB_ID10      = A(0x021F291E)   # get-rect B id10 臂（space {13,20,27,23}，22B）
                                 # → 內嵌 10B 跳板 → OvHook_RectZhong（col 感知）
P_MOVEB_WRAP_MOVS = A(0x021F2D9A)   # 欄B move fn row3 X-wrap：col+dx<0 → #5 → #6
P_MOVEB_WRAP_CMP  = A(0x021F2D9E)   # 欄B move fn row3 X-wrap：col+dx>5 → 0 → >6
P_IDFN_B_CASE3    = A(0x021F2F20)   # 欄B idfn row3 col→id 臂（10B）原位換 10B
# ---- v7.6.15/16：寬格確認閃塊 +16px 觀測（bl 站點 → kmod 日誌鉤子）----
# 閃塊渲染器 0x021F18E4 全部 9 個外部 bl 站點，原位 4B bl 換 bl（kmod 包裝
# 轉儲 arg1-arg4+FC/col/row 後轉發原渲染器，渲染行為不變）。
# v7.6.15 只接 A/B（30A2/30CE）→ 真機實錘僅鍵盤初始化各觸發一次，每幀
# 懸停/確認走其餘站點 → v7.6.16 補全 C-I（反彙編核對同約定：棧參=[obj+0xFC]）。
P_RLOG_SITES = [
    (A(0x021F30A2), 'OvHook_RLogA'),   # mode=1 確認渲染（init 態，真機僅開盤一次）
    (A(0x021F30CE), 'OvHook_RLogB'),   # mode=r7 分支（同上）
    (A(0x021F3618), 'OvHook_RLogC'),   # mode=1（r3=[obj+0x100]）
    (A(0x021F365E), 'OvHook_RLogD'),   # mode=1
    (A(0x021F373A), 'OvHook_RLogE'),   # mode=1
    (A(0x021F3880), 'OvHook_RLogF'),   # mode=2
    (A(0x021F398E), 'OvHook_RLogG'),   # mode=1
    (A(0x021F3A48), 'OvHook_RLogH'),   # mode=1
    (A(0x021F3B50), 'OvHook_RLogI'),   # mode=2
]
RLOG_RENDERER     = A(0x021F18E4)   # 原渲染器入口（自檢：各站點原 bl 目標必為此）
# 注意：0x021F292E 起是 get-rect B 的 id0/無效公共出口（b002/2000/bd38），
# id10 臂只有 0x021F291E-0x021F292D 共 16B 可用（跳板 10B + 墊 6B）。
TBL_RECT_BASE    = A(0x021F2808)   # get-rect 跳表基（idx 值 = 目標-基）
TBL_RECT_10      = A(0x021F281A)   # idx10 → 中矩形跳板
TBL_RECT_9       = A(0x021F2818)   # idx9 → false（防禦性）
TBL_DISP_BASE    = A(0x021F37D2)   # 分發器跳表基
TBL_DISP_10      = A(0x021F37E4)   # idx10 → 郵箱臂跳板
# v7.5：TBL_DISP_9（@0x021F37DE）寫入已廢除！真表 base=0x021F37D0（idx_i 條目
# 位於 0x021F37D0+2i，目標=0x021F37D2+off）：@37DE = 真 idx7 → 0x021F3828
# → E4=5 = 原生退格臂（B 鍵 poll_event 找 id7 格 → 分發 idx7 → E4=5）。
# v7.2-v7.4 按舊錯位解碼把它當 idx9 寫成公共出口 → id7 事件（B/觸摸/A 的
# 退格）全被吞，退格只能靠插件改 EXPH 兜底（無聲+卡頓）。「E4=5 消費者
# 無效」結論作廢——E4 從未被寫過。v7.5 起不動 @37DE，原生退格鏈
# （含退格聲與空串錯誤聲，均在 E4=5 消費者）自動恢復。

HOOKS = ['OvHook_IdModeBar', 'OvHook_SnapEnter', 'OvHook_SnapLeave',
         'OvHook_RectZhong', 'OvHook_DispatchZhong',
         'OvHook_RLogA', 'OvHook_RLogB', 'OvHook_RLogC', 'OvHook_RLogD',
         'OvHook_RLogE', 'OvHook_RLogF', 'OvHook_RLogG', 'OvHook_RLogH',
         'OvHook_RLogI']  # v7.6.15/16 閃塊觀測（純診斷）
# （OvHook_HitRectFix 已於 v7.6.3 從補丁集移除；C 源碼中函數體保留為死代碼，
#   ov237 不需重構——避免 kmod 重鏈與內嵌地址漂移風險。）
# v7.6.8：FlashFix bl 鉤子全撤（v7.6.6/v7.6.7 兩版真機實測均「進鍵盤即全格
# 常亮、選中反不亮」，且跨模組 bl+C 呼叫結構本身即嫌疑）——ov237/kmod 回到
# v7.6.5 字節級原樣。v7.6.9：cave 亦撤（真機否決，見 2b 節註釋）。


def bl_enc(at, target):
    """Thumb BL 編碼（目標為 kmod 內 Thumb 鉤子）。"""
    off = target - (at + 4)
    assert -(4 << 20) <= off < (4 << 20), f'bl 越界 {at:#x}->{target:#x}'
    return struct.pack('<HH', 0xF000 | ((off >> 12) & 0x7FF),
                       0xF800 | ((off >> 1) & 0x7FF))


def blx_enc(at, target):
    """Thumb BLX 編碼（J1/J2 擴展位正確處理負偏移；hw2 基 0xE800）。
    自校驗：對原生站點 197A/1990 → 0x2041208 編碼必須逐字節復現
    F64F/EC46、F64F/EC3A（main 中斷言）。"""
    pc = (at + 4) & ~3                     # BLX 目標計算用 Align(PC,4)
    off = target - pc
    assert off % 2 == 0 and -(16 << 20) <= off < (16 << 20), \
        f'blx 越界 {at:#x}->{target:#x}'
    u = (off >> 1) & 0xFFFFFF              # S:I1:I2:imm10H:imm10L 24位場
    s  = (u >> 23) & 1
    i1 = (u >> 22) & 1
    i2 = (u >> 21) & 1
    j1 = ((~i1) ^ s) & 1                   # I1 = NOT(J1^S) → J1 = NOT(I1^S)
    j2 = ((~i2) ^ s) & 1
    hw1 = 0xF000 | (s << 10) | ((u >> 11) & 0x3FF)
    hw2 = 0xE800 | (j1 << 13) | (j2 << 11) | (u & 0x7FF)
    return struct.pack('<HH', hw1, hw2)


def hook_addresses():
    out = subprocess.run([OBJDUMP, ELF], capture_output=True, text=True).stdout
    addrs = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] in HOOKS:
            addrs[parts[-1]] = int(parts[0], 16) | 1   # Thumb 位
    missing = [h for h in HOOKS if h not in addrs]
    if missing:
        raise SystemExit(f'ELF 缺少鉤子符號: {missing}')
    return addrs


def thumb_trampoline(target_addr, at):
    """ldr r3,[pc,#imm]; bx r3 (+ nop) + 4B 對齊字面量（Thumb16 無 ldr pc！）。
    at%4==0 → 8B：lit 在 at+4；at%4==2 → 10B：lit 在 at+6（溢出 2B，
    由呼叫端保證溢出字節落在鉤子重實現覆蓋區內）。
    返回 (code_bytes, literal_addr)。"""
    if at % 4 == 0:
        # lit = Align(at+4,4)+0 = at+4
        return struct.pack('<HHI', 0x4B00, 0x4718, target_addr), at + 4
    else:
        # lit = Align(at+4,4)+4 = (at+2)+4 = at+6
        return struct.pack('<HHHI', 0x4B01, 0x4718, 0x46C0, target_addr), at + 6


def tbl_off(table_base, target):
    v = target - table_base
    assert 0 <= v <= 0x7FFF, f'跳表偏移越界 {v:#x}'
    return v


def main():
    hooks = hook_addresses()
    print('鉤子地址:', {k: hex(v) for k, v in hooks.items()})

    # 永遠從原始未補丁鏡像出發（可重複運行）；下面的特徵 assert 會防呆
    img = bytearray(open(OV_ROM_IMG, 'rb').read())
    assert len(img) == 0x4300, f'ov194 鏡像大小異常 {len(img):#x}'

    def wr(addr, data):
        off = addr - BASE
        img[off:off + len(data)] = data

    def rd16(addr):
        return struct.unpack_from('<H', img, addr - BASE)[0]

    # ---- 1. move fn 模式欄迴圈 6→7 ----
    assert rd16(P_MOVE_WRAP_MOVS) == 0x2506, 'movs r5,#6 特徵不符'
    wr(P_MOVE_WRAP_MOVS, struct.pack('<H', 0x2507))
    assert rd16(P_MOVE_WRAP_CMP) == 0x2D06, 'cmp r5,#6 特徵不符'
    wr(P_MOVE_WRAP_CMP, struct.pack('<H', 0x2D07))

    # ---- 1b. get-rect 尾檢查 col>6→0 殺掉 col7(完成) 矩形 ----
    # 原始：所有臂落入 0x021F2872：row==5 && col>6 → 返回0。
    # col7（完成）矩形被丟棄 → 高亮不動（key_handler 35F8/363E 返回0即跳過
    # 重繪）、A 掃描區缺失、觸摸命中測試（0x021F2548 逐格 get-rect）失效。
    assert rd16(P_RECT_TAIL_CMP) == 0x2806, 'get-rect 尾檢查 cmp r0,#6 特徵不符'
    wr(P_RECT_TAIL_CMP, struct.pack('<H', 0x2807))

    # ---- 1c. v7.6.3：寬格臂原生斷言（v7.6.1 左移、v7.6.2 HitRectFix 均撤）----
    # 返口臂 0x021F284A 原生 adds r2,r0,#3(1CC2)、完成臂 0x021F2868 原生
    # adds r2,#0xB(320B)。get-rect 全消費者（掃描/光標括號框/pal3 確認閃光）
    # = v7.6 原生行為。
    assert rd16(A(0x021F284A)) == nat(A(0x021F284A))[0], '返口臂應保持原生 adds r2,r0,#3'
    assert rd16(A(0x021F2868)) == nat(A(0x021F2868))[0], '完成臂應保持原生 adds r2,#0xB'

    # ---- 1d. v7.6.5：row5 高亮塊 X 校正撤銷（保持原生 0x1CC7）----
    # v7.6.4a 實測：#3→#1 把前5模式格指示塊錯誤左移 16px（FC=0-4 本就
    # 正確），返口/完成 +16px 根因在貼圖層（MBRU 重建失效，插件側已修）
    # → X 校正兩頭落空，撤銷寫入，僅保留原生斷言防呆。
    assert rd16(P_FLASH_X) == nat(P_FLASH_X)[0], '高亮塊 X 計算應保持原生 adds r7,r0,#3'

    # ---- 1e. v7.6.10：欄B（漢字屏）解劫持——原生 idx9 臂必須存活 ----
    # 欄B 走獨立路徑（idfn 0x021F2EC8 + get-rect 0x021F288C，本補丁器不碰），
    # 但與欄A 共享分發器 0x021F37A0：
    #   - id9（欄B row2 兩端鍵）臂體在 0x021F3830-0x021F3849——v7 把郵箱
    #     跳板體壓在其前 8B → id9 事件被劫持。本版不再覆蓋，斷言原生字節：
    for at, hw in zip((A(0x021F3830), A(0x021F3832), A(0x021F3834), A(0x021F3836)),
                      nat(A(0x021F3830))):
        assert rd16(at) == hw, f'{at:#x} 原生 idx9 臂應保持原生 {hw:#x}（per-variant）'
    #   - id10（欄B space row3col5；欄A 中 col5row5）表項 @0x021F37E4 原生
    #     0x0034→0x021F3806（E4=9/E0=1 space 臂，臂體完好）→ 改指守衛跳板：
    #     row==5→郵箱；其餘→插件鉤子直返 0x021F3806|1（見 OvHook_DispatchZhong）
    #   - 跳板體新家 0x021F560C（TRAMP_RECT 只佔 5604-560B，其後應為零）
    assert all(b == 0 for b in img[A(0x021F560C) - BASE:A(0x021F5614) - BASE]), \
        'A(0x021F560C) 口袋應為零（TRAMP_RECT 後半）'

    # ---- 1f. v7.6.12：欄B（漢字屏 row3）插「中」----
    # ①idfn 0x021F2EC8 case3 臂原位 10B 換 10B：col0-4→id2-6 不變、
    #   col5(中)/col6(space)→id10。原生：ldr r0,[r0]; movs r3,#10;
    #   cmp r0,#5; beq 出口; adds r3,r0,#2。新：col+2≤6 即返回，否則 id10。
    for at, hw in ((A(0x021F2F20), 0x6800), (A(0x021F2F22), 0x230A),
                   (A(0x021F2F24), 0x2805), (A(0x021F2F26), 0xD000),
                   (A(0x021F2F28), 0x1C83)):
        assert rd16(at) == hw, f'idfn B case3 {at:#x} 應原生 {hw:#x}'
    wr(P_IDFN_B_CASE3, struct.pack('<5H',
        0x6800,   # ldr r0,[r0]        ; col
        0x1C83,   # adds r3,r0,#2      ; r3=col+2
        0x2B06,   # cmp r3,#6
        0xD900,   # bls 出口(2F2A)     ; col0-4 → id2-6（不變）
        0x230A))  # movs r3,#10        ; col5(中)/col6(space) → id10
    # ②get-rect B 尾檢查：row3 && col>5 → 無效 改 col>6（col6=space）
    assert rd16(P_RECTB_TAIL_CMP) == 0x2805, 'get-rect B 尾檢查應原生 cmp r0,#5'
    wr(P_RECTB_TAIL_CMP, struct.pack('<H', 0x2806))
    # ③get-rect B id10 臂（space）→ 內嵌跳板 → OvHook_RectZhong。
    #   原生臂 16B：210D 6021 2014 310E 6060 60A1 1CC0 E7D8（佔 291E-292D，
    #   292E 起為 id0/無效公共出口 b002/2000/bd38 不可碰）。
    assert struct.unpack_from('<8H', img, P_RECTB_ID10 - BASE) == \
        (0x210D, 0x6021, 0x2014, 0x310E, 0x6060, 0x60A1, 0x1CC0, 0xE7D8), \
        'get-rect B id10 臂應原生'
    # ④欄B move fn row3 X-wrap：col+dx<0→5 改 6、col+dx>5→0 改 >6。
    #   （row3→row0/2 下行子跳表帶 bhi 越界守衛，col6 直通彈出，無需動。）
    assert rd16(P_MOVEB_WRAP_MOVS) == 0x2305, '欄B wrap movs 應原生 2305'
    assert rd16(P_MOVEB_WRAP_CMP) == 0x2B05, '欄B wrap cmp 應原生 2B05'
    wr(P_MOVEB_WRAP_MOVS, struct.pack('<H', 0x2306))
    wr(P_MOVEB_WRAP_CMP,  struct.pack('<H', 0x2B06))

    # ---- 1g. v7.6.15/16：閃塊渲染 bl 站點 → kmod 觀測鉤子（全部 9 個外部站點）----
    # 原生 bl 對解碼必須落在渲染器 0x021F18E4（防地址映射漂移），再原位
    # 4B 換 bl（OvHook_RLogA-I）。kmod 包裝轉儲後轉發原渲染器，行為不變。
    def bl_target(img_arr, at):
        h1, h2 = struct.unpack_from('<HH', img_arr, at - BASE)
        off = ((h1 & 0x7FF) << 11) | (h2 & 0x7FF)
        if off & 0x200000:  # 22 位符號擴展
            off -= 1 << 22
        return at + 4 + off * 2
    for at, hook in P_RLOG_SITES:
        t = bl_target(img, at)
        assert t == RLOG_RENDERER, f'{at:#x} 原 bl 目標應為渲染器，實 {t:#x}'
        wr(at, bl_enc(at, hooks[hook]))

    # ---- 2. 跳板（ldr r3,[pc,#imm]; bx r3 + 對齊字面量）----
    for at, hook in ((TRAMP_SNAP_IN,  'OvHook_SnapEnter'),
                     (TRAMP_SNAP_OUT, 'OvHook_SnapLeave'),
                     (TRAMP_IDMODE,   'OvHook_IdModeBar'),
                     (TRAMP_IDMODE2,  'OvHook_IdModeBar'),
                     (TRAMP_RECT,     'OvHook_RectZhong'),
                     (TRAMP_DISPATCH, 'OvHook_DispatchZhong'),
                     (P_RECTB_ID10,   'OvHook_RectZhong')):  # v7.6.12 欄B 內嵌
        code, lit_at = thumb_trampoline(hooks[hook], at)
        wr(at, code)
        wr(lit_at, struct.pack('<I', hooks[hook]))
    # v7.6.12：id10 臂殘餘 6B（0x021F2928-292D，原 60A1/1CC0/E7D8 死碼）墊 nop
    wr(A(0x021F2928), struct.pack('<3H', 0x46C0, 0x46C0, 0x46C0))

    # （v7.9 的 2b FC 跳板 ×4、v8.0 的 2c RenderEntry、v8.1 的 2d BlockW bl
    #   鉤子已於 v7.6.1 全部撤銷——回到 v7.6 補丁集。）

    # ---- 2b. v7.6.9：畫點站點全部保持原生（v7.6.8 cave 真機否決回滾）----
    # v7.6.8 cave 三重編碼錯誤（真機實錘）：0xD004(X==15→blx 不校正)、
    # 0xD000(是 BEQ 非 BNE——X≠25 跌入 subs)、0x1E49(subs r1,#1 非 #2)
    # → 小格確認閃塊全錯誤左移 8px、寬格未被校正。v7.6.6/7/8 三連否決
    # → 寬格 +16px 修正懸置，站點全部恢復原生（= v7.6.5 補丁集）。
    for at in (A(0x021F197A), A(0x021F1990), A(0x021F19D6), A(0x021F1A00)):
        got = struct.unpack_from('<HH', img, at - BASE)
        assert got == nat(at), \
            f'{at:#x} 畫點站點應保持原生 {nat(at)}，實 {got}'

    # ---- 3. 跳表重指向 ----
    # get-rect: idx9(死 id)→false 0x021F286C、idx10(中)→尾口袋跳板
    wr(TBL_RECT_9,  struct.pack('<H', tbl_off(TBL_RECT_BASE, A(0x021F286C))))
    wr(TBL_RECT_10, struct.pack('<H', tbl_off(TBL_RECT_BASE, TRAMP_RECT)))
    # 分發器: 僅 idx10(中/space 共用 id)→守衛跳板。真 idx7（退格臂）與
    # idx9 臂體均保持原生（見上方 1e 節註釋）。
    wr(TBL_DISP_10, struct.pack('<H', tbl_off(TBL_DISP_BASE, TRAMP_DISPATCH)))

    # ---- 4. 重壓縮寫回 ----
    comp = bytes(CC.compress(bytes(img)))
    open(OV_FILE, 'wb').write(comp)
    print(f'ov194 補丁完成: {len(comp):#x} 字節（原 0x357C）')

    # ---- 5. 更新表項 compressedSize（低 24 位），flags 位元組不動 ----
    tbl = bytearray(open(TBL_FILE, 'rb').read())
    off = OV_ID * 32 + 0x1C
    old = struct.unpack_from('<I', tbl, off)[0]
    flags = old >> 24
    struct.pack_into('<I', tbl, off, (flags << 24) | (len(comp) & 0xFFFFFF))
    open(TBL_FILE, 'wb').write(bytes(tbl))
    print(f'overlay 表 ov{OV_ID}: compSz {old & 0xFFFFFF:#x} → {len(comp):#x}, flags={flags:#x} 不變')

    # ---- 6. 自檢：解壓回讀關鍵字節 ----
    chk = CC.decompress(open(OV_FILE, 'rb').read())
    assert chk == bytes(img), '重壓縮回讀不一致！'
    print('自檢 OK: 補丁鏡像往返一致')
    for name, at, hook in (('SnapEnter', TRAMP_SNAP_IN, 'OvHook_SnapEnter'),
                           ('SnapLeave', TRAMP_SNAP_OUT, 'OvHook_SnapLeave'),
                           ('IdMode',    TRAMP_IDMODE,  'OvHook_IdModeBar'),
                           ('IdMode2',   TRAMP_IDMODE2, 'OvHook_IdModeBar'),
                           ('Rect',      TRAMP_RECT,    'OvHook_RectZhong'),
                           ('Dispatch',  TRAMP_DISPATCH, 'OvHook_DispatchZhong')):
        # 顯式驗證：hw0=ldr r3 literal、hw1=bx r3、字面量=鉤子|1
        hw0 = struct.unpack_from('<H', chk, at - BASE)[0]
        hw1 = struct.unpack_from('<H', chk, at - BASE + 2)[0]
        lit_at = at + 4 if at % 4 == 0 else at + 6
        assert hw0 == (0x4B00 if at % 4 == 0 else 0x4B01), f'{name} ldr 編碼錯 {hw0:#x}'
        assert hw1 == 0x4718, f'{name} bx 編碼錯 {hw1:#x}'
        got = struct.unpack_from('<I', chk, lit_at - BASE)[0]
        assert got == hooks[hook], f'{name} 跳板字面量錯 {got:#x}'
        # 跳轉鏈模擬：字面量 bx 後落在鉤子首指令
        assert got & 1 and (got & ~1) != 0, f'{name} 鉤子地址缺 Thumb 位'
        print(f'  {name}: @{at:#x} ldr r3,[pc,#{4 if at%4 else 0}]; bx r3 → {got:#x} ✓')

    # ---- 7. v7.6.1 寬格高亮左移自檢 + 原生復原自檢 ----
    # idx7/idx8 臂尾跳應為原生 e012/e010（→3852）；口袋 3838 應為原生殘骸
    b7 = struct.unpack_from('<H', chk, A(0x021F382A) - BASE)[0]
    b8 = struct.unpack_from('<H', chk, A(0x021F382E) - BASE)[0]
    assert b7 == nat(A(0x021F382A))[0], f'idx7 臂尾跳應原生，實 {b7:#x}'
    assert b8 == nat(A(0x021F382E))[0], f'idx8 臂尾跳應原生，實 {b8:#x}'
    pk = struct.unpack_from('<HHHH', chk, A(0x021F3838) - BASE)
    assert pk == nat(A(0x021F3838)), f'口袋 3838 應為原生殘骸 {pk}'
    assert struct.unpack_from('<H', chk, A(0x021F3858) - BASE)[0] == nat(A(0x021F3858))[0], '3858 應為原生 adds r5,#8'
    # v7.6.10：原生 idx9 臂（0x021F3830-37）在成品中必須為原生字節
    chk_arm = struct.unpack_from('<4H', chk, A(0x021F3830) - BASE)
    assert chk_arm == nat(A(0x021F3830)), \
        f'idx9 臂應原生，實 {chk_arm}'
    # idx10 表項應指向守衛跳板 0x021F560C（off=0x1E3A，base 0x021F37D2）
    e10 = struct.unpack_from('<H', chk, TBL_DISP_10 - BASE)[0]
    assert e10 == (TRAMP_DISPATCH - TBL_DISP_BASE) & 0xFFFF, \
        f'idx10 表項應={TRAMP_DISPATCH - TBL_DISP_BASE:#x}，實 {e10:#x}'
    # 郵箱跳板 @0x021F560C：ldr r3,[pc]; bx r3; 字面量=OvHook_DispatchZhong
    d0 = struct.unpack_from('<H', chk, TRAMP_DISPATCH - BASE)[0]
    assert d0 == 0x4B00, f'郵箱跳板 ldr 編碼錯 {d0:#x}'
    d_lit = struct.unpack_from('<I', chk, TRAMP_DISPATCH + 4 - BASE)[0]
    assert d_lit == hooks['OvHook_DispatchZhong'], '郵箱跳板字面量錯'
    w1 = struct.unpack_from('<H', chk, A(0x021F284A) - BASE)[0]
    w2 = struct.unpack_from('<H', chk, A(0x021F2868) - BASE)[0]
    assert w1 == nat(A(0x021F284A))[0], f'返口臂應為原生 adds r2,r0,#3，實 {w1:#x}'
    assert w2 == nat(A(0x021F2868))[0], f'完成臂應為原生 adds r2,#0xB，實 {w2:#x}'
    # 渲染器 0x021F18E4 內部：四個畫點站點全部原生（v7.6.9 = v7.6.5 補丁集）
    assert struct.unpack_from('<4H', chk, A(0x021F1900) - BASE) == nat(A(0x021F1900)), \
        '渲染器參準備段應為原生'
    for at in (A(0x021F1978), A(0x021F198E)):
        got = struct.unpack_from('<4H', chk, at - BASE)
        assert got == nat(at), f'{at:#x} 畫點站點應原生 {nat(at)}，實 {got}'
    for at in (A(0x021F19D6), A(0x021F1A00)):
        got = struct.unpack_from('<HH', chk, at - BASE)
        assert got == nat(at), f'{at:#x} pal3 站點應原生 {nat(at)}，實 {got}'
    print('  渲染器: 畫點站點 197A/1990/19D6/1A00 全部原生 ✓（v7.6.6/7/8 鉤子全撤）')
    # v7.6.5 高亮塊 X 原生自檢：0x021F1968 應為原生 adds r7,r0,#3(1CC7)
    fx = struct.unpack_from('<H', chk, P_FLASH_X - BASE)[0]
    assert fx == nat(P_FLASH_X)[0], f'高亮塊 X 應保持原生 adds r7,r0,#3，實 {fx:#x}'
    # ---- 8. v7.6.3 命中測試 bl 原生自檢（重定向已撤）----
    for at in (A(0x021F2570), A(0x021F261A)):
        h1, h2 = struct.unpack_from('<HH', chk, at - BASE)
        assert (h1, h2) == nat(at), f'{at:#x} bl 應為原生 {nat(at)}，實 {h1:04x} {h2:04x}'
        off = ((h1 & 0x7FF) << 11) | (h2 & 0x7FF)
        tgt = at + 4 + off * 2
        assert tgt == A(0x021F2704), f'{at:#x} bl 應指向原生 thunk，實 {tgt:#x}'
    print(f'  v7.6.3: 寬格臂原生 ✓（返口 {w1:#06x}、完成 {w2:#06x}）；'
          f'命中測試 bl ×2 原生→thunk ✓；idx7/idx8 原生 ✓；3858 原生 ✓')
    print(f'  v7.6.5: 高亮塊 X 保持原生 ✓（{P_FLASH_X:#x} = 0x1CC7）；MBRU 容差重建保留')
    print(f'  v7.6.9: 畫點站點全原生；RectZhong 跳板回 {TRAMP_RECT:#x}（v7.6.8 cave 否決）；'
          f'ov237/kmod = v7.6.5 字節級（dd48f46e）✓')
    # v7.6.12：欄B 插中五處自檢
    assert struct.unpack_from('<5H', chk, P_IDFN_B_CASE3 - BASE) == \
        (0x6800, 0x1C83, 0x2B06, 0xD900, 0x230A), 'idfn B case3 應為 col5/6→id10 新編碼'
    assert struct.unpack_from('<H', chk, P_RECTB_TAIL_CMP - BASE)[0] == 0x2806, \
        'get-rect B 尾檢查應為 cmp r0,#6'
    assert struct.unpack_from('<HH', chk, P_RECTB_ID10 - BASE) == (0x4B01, 0x4718), \
        'get-rect B id10 跳板 ldr/bx 編碼錯'
    b_lit = struct.unpack_from('<I', chk, P_RECTB_ID10 + 6 - BASE)[0]
    assert b_lit == hooks['OvHook_RectZhong'], f'欄B 跳板字面量錯 {b_lit:#x}'
    assert struct.unpack_from('<3H', chk, A(0x021F2928) - BASE) == (0x46C0, 0x46C0, 0x46C0), \
        'id10 臂殘餘應為 nop 墊'
    assert struct.unpack_from('<H', chk, A(0x021F292E) - BASE)[0] == 0xB002, \
        '292E 公共出口（add sp,#8）不可被碰'
    assert struct.unpack_from('<3H', chk, P_MOVEB_WRAP_MOVS - BASE) == (0x2306, 0xE002, 0x2B06), \
        '欄B move fn wrap 應為 #6 新編碼'
    print(f'  v7.6.12: 欄B 插中五處 ✓（idfn case3 col5/6→id10、get-rect B 尾檢 #6、'
          f'id10 臂內嵌跳板→{b_lit:#x}、move row3 wrap #6）')
    print(f'  v7.6.10: idx9 臂 A(0x021F3830) 原生 ✓（欄B row2 兩端鍵解劫持）；'
          f'郵箱跳板遷 {TRAMP_DISPATCH:#x}、idx10 表項 off={e10:#x} ✓（欄B space 解劫持，'
          f'守衛 row==5→郵箱 / 其餘→原生 space 臂 A(0x021F3806)）')
    # v7.6.15/16：閃塊觀測 bl 重定向自檢（回讀解碼 → 鉤子地址；全 9 站點）
    for name, (at, hook) in zip('ABCDEFGHI', P_RLOG_SITES):
        h1, h2 = struct.unpack_from('<HH', chk, at - BASE)
        off = ((h1 & 0x7FF) << 11) | (h2 & 0x7FF)
        if off & 0x200000:
            off -= 1 << 22
        tgt = at + 4 + off * 2
        assert tgt == hooks[hook] & ~1, f'RLog{name} bl 應指向 {hooks[hook]:#x}，實 {tgt:#x}'
        print(f'  v7.6.15/16 RLog{name}: @{at:#x} bl → {tgt:#x} ✓（轉發渲染器 {RLOG_RENDERER:#x}）')


if __name__ == '__main__':
    main()
