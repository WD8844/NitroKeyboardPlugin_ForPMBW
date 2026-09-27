#include "kbi_internal.h"

/* ============================================================
 * 「中」圖標：遊戲鍵盤模式欄（BG2，引擎A）新增第 6 格（緊鄰ロ-カ字右側）
 * 平假名格貼圖保持原生（貼圖替換版已完成使命並驗證通過，本版回退）
 *
 * 顯示配置（IO 寄存器確認）：BG2 4bpp, 256x256，
 *   地圖 SBB4=0x06002000, 圖塊 CBB1=0x06004000
 *   模式欄 = 地圖 rows 20-22（螢幕 y160-184），5 格每格 2 圖塊寬：
 *     か+よ/カナ/ABC/1/?/ロ-カ字 = tx3-12
 *   「返口」「完成」按鈕 = tx13-20 / tx21-28，右端封條 tx29-30
 * 新格（tx13-14）：頂=0x040,0x041  腰=新「中」圖塊 0x300,0x301  底=0x050,0x051
 *   圖塊 0x300/0x301 在 CBB1 空閒區（0x067-0x3FF 全零且不被 SBB0/2/4 引用）
 * 按鈕右移：rows20-22 的 tx13-28 → tx15-30（從右往左複製防重疊覆蓋）
 * 觸摸判定：遊戲判定區不隨視覺移動——點「中」格可能同時觸發遊戲「返口」
 * 重新檢測：遊戲重繪地圖（切模式等）後 tx13 恢復原生特徵 01D/023/02D → 自動重做
 * 注：2026-09-04 下午的「卡死」均為 kmod 未進 nitrofs 的構建錯誤，與本方案無關
 * ============================================================ */
#define MB_MAP_BASE   0x06002000u
/* v7.6.14：欄B（漢字屏）模式欄真身在 BG1/SBB2（探測版定位，見 UpdateBarB）*/
#define MB_MAP_BASE_B 0x06001000u
#define MB_TIL_BASE   0x06004000u
#define MB_ROW        20          /* 模式欄頂行（佔 20/21/22 三行）*/
#define MB_CELL6_TX   13          /* 新格 tx13-14（x104-120）*/
#define MB_ZH_TILE_L  0x300
#define MB_ZH_TILE_R  0x301
#define MB_ZH_TOPL    0x302       /* 頂行（確認版清理過的 03C/03D 拷貝）*/
#define MB_ZH_TOPR    0x303
#define MB_ZH_BOTL    0x304       /* 底行（04C / 清理過的 04D 拷貝）*/
#define MB_ZH_BOTR    0x305

/* 「中」16x8 圖塊 ×2：**沿用用戶逐像素確認的 cell1 預覽版圖形**
 * （_cell1_preview.py 生成、真機驗證通過）：含左右邊框列
 * （a/x/…/x/a/b，與原生格完全同款），內容區 12 列畫「中」
 * （字庫 glyph628 結構 1:1：框橫筆 x3-12、框豎筆 x3-4/x11-12、
 * 豎筆 x7-8 全高）。1=筆畫色 B=背景色；整圖塊賦值 = 先清再畫 */
static const u32 ZhongTileL[8] = {
    0x1BBBBBA2, 0x1BBBBBA2, 0x11111BA2, 0x1BB11BA2,
    0x11111BA2, 0x1BBBBBA2, 0x1BBBBBA3, 0x1BBBBBA3,
};
static const u32 ZhongTileR[8] = {
    0x02ABBBB1, 0x02ABBBB1, 0x02A11111, 0x02A11BB1,
    0x02A11111, 0x02ABBBB1, 0x03ABBBB1, 0x03ABBBB1,
};
/* 頂/底行：確認版清理數據（03C 清 y7 殘筆、04D 清 y0 殘筆+漸變），
 * 從 VRAM 快照烘焙（不可直接覆蓋 03C/03D/04C/04D——多格共用）*/
static const u32 ZhongTopL[8] = {
    0x00000000, 0x00000000, 0x22222225, 0xAAAAAA82,
    0xBBBBBAA2, 0xBBBBBBA2, 0xBBBBBBA2, 0xBBBBBBA2,
};
static const u32 ZhongTopR[8] = {
    0x00000000, 0x00000000, 0x05222222, 0x028AAAAA,
    0x02AABBBB, 0x02ABBBBB, 0x02ABBBBB, 0x02ABBBBB,
};
static const u32 ZhongBotL[8] = {
    0xBBBBBBA4, 0xBBBBBBA4, 0xBBBBBBA4, 0xBBBBBAA4,
    0xAAAAAA84, 0x44444445, 0x00000000, 0x00000000,
};
static const u32 ZhongBotR[8] = {
    0x04ABBBBB, 0x04ABBBBB, 0x04ABBBBB, 0x04AABBBB,
    0x048AAAAA, 0x05444444, 0x00000000, 0x00000000,
};

/* 每幀調用：v7.6.14 起 = UpdateBarB（欄B/SBB2）+ 本函數欄A（BG2/SBB4）路徑。
 * 欄A：檢測模式欄 → 快照 → 右移按鈕（僅首次/被重繪後）→ 貼新格
 * v7.4：完好判據改為「中格(tx13) + 右移態(tx15)」雙驗證——舊版只驗 tx13，
 * 遊戲部分重繪（覆蓋 tx15-30 但保留 tx13-14）時死鎖在錯位態
 * （真機 2026-09-09 實測：貼圖在原生位、框/確認區在右移位）。
 * 損壞態從原生條目快照重建，不再依賴 tx13 恢復原生特徵。 */
static u16 gMbSnap[3][28];   /* 原生 tx3-30 地圖條目快照（右移前；0-9=五模式格 tx3-12，
                              * 10-27=寬鍵區 tx13-30。v7.6 擴展：模式格作為「仍在
                              * 模式欄屏」判據，防離屏誤重建）*/
static bool gMbSnapValid;

/* ---- v7.6.14 欄B（漢字屏）模式欄插中——真機 2026-09-12 探測定位 ----
 * v7.6.13 探測版實錘（TTY 2026-09-12）：
 *  1. 開盤（欄A）：遊戲把模式欄畫進 BG2/SBB4 rows20-22，隨後本插件右移+
 *     貼中（tx13=中 302/300/304、tx15=01D/023/02D）——欄A 正常。全程
 *     SBB4 rows20-22 只此一態，遊戲在欄B 期間不碰 SBB4。
 *  2. 選「漢字」進欄B（KDOWN A @893615）：@896927 遊戲把欄B 模式欄畫進
 *     BG1/SBB2 rows20-22（原生態 tx13=01D/023/02D、tx15=01E/028/02E，
 *     tx3=03C/042/04C 與欄A 模式格同 tile）。
 *  3. 同步 DISPCNT 1F10→1B10：BG2 顯示關閉、BG3 開啟——v7.6.12 寫 SBB4
 *     的貼圖落在關閉的圖層上，這就是「中格有、貼圖無」的根因。
 * 兩 BG 同用 CBB1（0x06004000），「中」tile 數據由欄A 公共段每幀重寫，
 * 欄B 只需寫地圖條目（pal1 同款）。欄B space = 寬格 tx13-27（get-rect
 * x13-27 實測），就地向右移 2 tile → tx15-29，貼中 tx13-14。
 * MBD2 = 首次偵到原生態時 rows19-23 全行轉儲（驗證 tx28-31 右緣內容）。 */
static bool gMbDumpBDone;
static void DumpBarBRows(void) {
    vu16 *mapB = (vu16 *)MB_MAP_BASE_B;
    for (int r = 19; r <= 23; r++) {
        vu16 *row = mapB + r * 32;
        for (int c = 0; c < 32; c += 2) {
            char tag[8];
            tag[0] = 'M'; tag[1] = 'B'; tag[2] = '2'; tag[3] = 'R';
            tag[4] = (char)('0' + r / 10); tag[5] = (char)('0' + r % 10);
            tag[6] = 0;
            TtyLog(tag, (u32)row[c], (u32)row[c + 1]);
        }
    }
}
void UpdateBarB(void) {
    vu16 *mapB = (vu16 *)MB_MAP_BASE_B;
    u16 eB[3], sB[3];
    for (int r = 0; r < 3; r++) {
        eB[r] = mapB[(MB_ROW + r) * 32 + MB_CELL6_TX] & 0x3FF;       /* tx13 */
        sB[r] = mapB[(MB_ROW + r) * 32 + MB_CELL6_TX + 2] & 0x3FF;   /* tx15 */
    }
    bool nativeB = (eB[0] == 0x01D && eB[1] == 0x023 && eB[2] == 0x02D &&
                    sB[0] == 0x01E && sB[1] == 0x028 && sB[2] == 0x02E);
    bool zhongB  = (eB[0] == MB_ZH_TOPL && eB[1] == MB_ZH_TILE_L &&
                    eB[2] == MB_ZH_BOTL &&
                    sB[0] == 0x01D && sB[1] == 0x023 && sB[2] == 0x02D);
    if (zhongB || !nativeB) return;   /* 已插中 / 欄B 模式欄未顯示 */
    if (!gMbDumpBDone) { gMbDumpBDone = 1; DumpBarBRows(); }
    /* v7.6.26：與 SyncRamMaps 同方案——三行統一搬 14 列（c13-26→c15-28，
     * space 鍵整體右移 2 列），花紋（c27-31）原生不動=裁掉左側 2 列
     * （詳見 SyncRamMaps 同註；本函數僅 SyncRamMaps 未生效時的後備）*/
    for (int r = 0; r < 3; r++) {
        vu16 *row = mapB + (MB_ROW + r) * 32;
        for (int i = 13; i >= 0; i--)
            row[MB_CELL6_TX + 2 + i] = row[MB_CELL6_TX + i];
    }
    /* 貼中 tx13-14（pal1，與欄A 同款；tile 數據共用 CBB1）*/
    mapB[(MB_ROW + 0) * 32 + MB_CELL6_TX]     = (u16)(0x1000 | MB_ZH_TOPL);
    mapB[(MB_ROW + 0) * 32 + MB_CELL6_TX + 1] = (u16)(0x1000 | MB_ZH_TOPR);
    mapB[(MB_ROW + 1) * 32 + MB_CELL6_TX]     = (u16)(0x1000 | MB_ZH_TILE_L);
    mapB[(MB_ROW + 1) * 32 + MB_CELL6_TX + 1] = (u16)(0x1000 | MB_ZH_TILE_R);
    mapB[(MB_ROW + 2) * 32 + MB_CELL6_TX]     = (u16)(0x1000 | MB_ZH_BOTL);
    mapB[(MB_ROW + 2) * 32 + MB_CELL6_TX + 1] = (u16)(0x1000 | MB_ZH_BOTR);
    /* v7.6.25：尾部原生不動，不再回寫 1003@tx31（見 SyncRamMaps 同註）*/
    TtyLog("MBRB2", 0, 0);
}

void UpdateModeBarIcon(void) {
    vu16 *map = (vu16 *)MB_MAP_BASE;
    u16 e[3], s[3];
    for (int r = 0; r < 3; r++) {
        e[r] = map[(MB_ROW + r) * 32 + MB_CELL6_TX] & 0x3FF;       /* tx13 */
        s[r] = map[(MB_ROW + r) * 32 + MB_CELL6_TX + 2] & 0x3FF;   /* tx15 */
    }
    bool native  = (e[0] == 0x01D && e[1] == 0x023 && e[2] == 0x02D);
    bool shifted = (s[0] == 0x01D && s[1] == 0x023 && s[2] == 0x02D);

    /* stale 指針界限校驗淘汰（僅維護 gKbCur；活動欄判定已改地圖特徵，v7.6.14）*/
    if (gKbCur) {
        u32 c = gKbCur[0], rw = gKbCur[1];
        if (rw > 5 || c > 7) gKbCur = NULL;
    }
    if (!gMbSnapValid) {
        if (native) {
            /* 原生佈局特徵（返口按鈕左緣 tx13）才快照 */
            for (int r = 0; r < 3; r++)
                for (int i = 0; i < 28; i++)
                    gMbSnap[r][i] = map[(MB_ROW + r) * 32 + 3 + i];
            gMbSnapValid = 1;
        } else if (!shifted) {
            /* v7.6.22 根因修復：SyncRamMaps 在遊戲 memcpy 前右移 RAM buffer
             * 後，VRAM 可能從未呈現原生態（v7.6.21 真機實錘：「中」地圖條目
             * 隨 memcpy 進 VRAM、功能全正常，但 CBB1 tile 數據整場不寫=中
             * 隱形——原快照門控把整個函數攔死在頭部）。已右移=模式欄在屏：
             * 繼續往下只做貼中+tile 每幀重寫（無快照則喪失損壞態重建後備，
             * 該路徑已由 SyncRamMaps RAM buffer 主修覆蓋）；未顯示才退出。 */
            gBlockModeTouch = 0;    /* 遊戲鍵盤未顯示/佈局被重繪 */
            return;
        }
    }

    if (gMbSnapValid && !shifted) {
        if (native) {
            /* 整體原生態：就地右移（tx13-28 → tx15-30，從右往左防重疊）*/
            for (int r = 0; r < 3; r++) {
                vu16 *row = map + (MB_ROW + r) * 32;
                for (int i = 15; i >= 0; i--)
                    row[MB_CELL6_TX + 2 + i] = row[MB_CELL6_TX + i];
            }
        } else {
            /* 損壞態（部分重繪/深度重繪含 tx13 被覆蓋）。v7.6 消滅原
             * else{gBlockModeTouch=0;return} 死鎖盲區：只要五模式格
             * （tx3-12，我們從不寫入）仍與快照一致 = 遊戲還在模式欄屏
             * 且只是把寬鍵區畫回原生位 → 一律從快照重建右移態。
             * 「貼圖原生位 + 框/確認區右移位」(+16 錯位) 即此場景。
             * v7.6.5 容差化：原生 30/30 全等判據永遠被 pal4 指示塊擊敗
             * ——模式指示塊（ pal4，2 tile 寬 ×3 行）落在 tx11-12 或
             * 確認閃塊落點一旦重疊模式格列，每幀 tilemap 重繪會污染
             * 最多 2-4 列×3 行 ≤12 項 → 全等判據恆假 → 重建從不觸發
             * → 遊戲重繪寬鍵後貼圖停在原生位（tx13-28）而框/確認區在
             * 右移位（tx15-30）。
             * 【v7.6.6 修正→v7.6.8 定案】上說「返口/完成 高亮 +16px
             * 根因」已被真機推翻（v7.6.5 容差重建上線後 +16px 原樣）
             * ——真根因是遊戲維護的 [obj+0xFC]=寬鍵 FC 6/7/11 使 pal4
             * 閃塊 X=FC*2+3 恆 +2 tile；此根因的修復位於 ov194 渲染器
             * 畫點站點的 ov194 內部 cave（_patch_ov194.py 2b 節），
             * 與本插件鉤子無關。本處容差重建仍保留：用戶實測其餘
             * 行為（快照重建觸發）正確。 */
            /* ≥18/30 項與快照一致即認定「仍在模式欄屏」，從快照重建；
             * <18 = 真的換屏了（其他 UI 覆蓋）→ 不動。 */
            int match = 0;
            for (int r = 0; r < 3; r++)
                for (int i = 0; i < 10; i++)
                    if ((map[(MB_ROW + r) * 32 + 3 + i] & 0x3FF) ==
                        (gMbSnap[r][i] & 0x3FF))
                        match++;
            if (match < 18) {
                gBlockModeTouch = 0;    /* 深度換屏，放棄 */
                return;
            }
            for (int r = 0; r < 3; r++)
                for (int i = 0; i < 16; i++)
                    map[(MB_ROW + r) * 32 + MB_CELL6_TX + 2 + i] = gMbSnap[r][10 + i];
            TtyLog("MBRU", 0, 0);
        }
    }
    /* 寫新格地圖條目（pal1，無翻轉）——三行全部用確認版專用圖塊 */
    map[(MB_ROW + 0) * 32 + MB_CELL6_TX]     = (u16)(0x1000 | MB_ZH_TOPL);
    map[(MB_ROW + 0) * 32 + MB_CELL6_TX + 1] = (u16)(0x1000 | MB_ZH_TOPR);
    map[(MB_ROW + 1) * 32 + MB_CELL6_TX]     = (u16)(0x1000 | MB_ZH_TILE_L);
    map[(MB_ROW + 1) * 32 + MB_CELL6_TX + 1] = (u16)(0x1000 | MB_ZH_TILE_R);
    map[(MB_ROW + 2) * 32 + MB_CELL6_TX]     = (u16)(0x1000 | MB_ZH_BOTL);
    map[(MB_ROW + 2) * 32 + MB_CELL6_TX + 1] = (u16)(0x1000 | MB_ZH_BOTR);
    /* 圖塊數據每幀重寫（整圖塊賦值 = 先清背景再畫字，防遊戲污染）*/
    u32 *tl = (u32 *)(MB_TIL_BASE + MB_ZH_TILE_L * 32);
    u32 *tr = (u32 *)(MB_TIL_BASE + MB_ZH_TILE_R * 32);
    u32 *tl2 = (u32 *)(MB_TIL_BASE + MB_ZH_TOPL * 32);
    u32 *tr2 = (u32 *)(MB_TIL_BASE + MB_ZH_TOPR * 32);
    u32 *bl2 = (u32 *)(MB_TIL_BASE + MB_ZH_BOTL * 32);
    u32 *br2 = (u32 *)(MB_TIL_BASE + MB_ZH_BOTR * 32);
    for (int i = 0; i < 8; i++) {
        tl[i] = ZhongTileL[i];
        tr[i] = ZhongTileR[i];
        tl2[i] = ZhongTopL[i];
        tr2[i] = ZhongTopR[i];
        bl2[i] = ZhongBotL[i];
        br2[i] = ZhongBotR[i];
    }
    gBlockModeTouch = 1;    /* 新格已生效 → 觸摸過濾/平移開啟 */
}
/* v7.6.20：+16px 根因修復——確認閃塊寫目標=遊戲自有 RAM map buffer。
 * v7.6.19 真機實錘：entry2（fill id=2 寫目標=欄A 鍵盤平面 BG2/SBB4 的 RAM
 * 鏡像）rows20-22=原生佈局（tx13=01D/023/02D、右緣 1C04@tx29/1003@tx30、
 * 無中），而 SBB4 顯示層同刻=右移+中態；entry1（fill id=1=欄B 平面 BG1/
 * SBB2）同刻=空白+指示塊（≡MA2R 逐項相等，鏡像關係坐實）。遊戲把 RAM
 * buffer memcpy（[en+0xC]=0x800）蓋回 VRAM，插件只在 VRAM 側右移——確認
 * 閃塊 fill(2/1, rect, pal3) 以正確 rect 塗進原生 buffer → 閃幀模式欄整
 * 體回原生位 → 相對右移貼圖恆 +16px（小格鍵 rows0-19 從不右移不受影響）。
 * 修法：把右移+貼中直接做進 RAM buffer 本體——偵到原生特徵才動作（冪等，
 * 遊戲重繪回原生後自動再觸發），entry2/entry1 分別按欄A 16 列（tx13-28→
 * tx15-30）/欄B 17 列（tx13-29→tx15-31，space 帶右框列，v7.6.16 定案）。
 * 中 tile 數據在 CBB1（VRAM），memcpy 只搬 map 不搬 tile，條目引用同 tile
 * 號即可。平面字節 [en+0x1D]=1（v7.6.19 FBx 實錘）→ stride32、row=idx/32。
 * VRAM 側 UpdateModeBarIcon/UpdateBarB 保留：偵測將見「已右移」而跳過。
 * v7.6.24：本函數不得無條件調用——簽名無法區分欄B 與數字(fc=3)/QWERTY
 * (fc=5) 屏，調用方（PollIconTouch/FlashRenderLog）必須以 FC 閘門
 * （gCurFC/fc == 0 或 4）先行過濾，詳見各調用點註釋。 */
void SyncRamMaps(void) {
    u32 tblBase = *(vu32 *)ADDR_FILL_ENTRY_TBL;
    if (tblBase < 0x02000000u || tblBase >= 0x03000000u) return;
    for (u32 e = 1; e <= 2; e++) {
        vu8 *en = (vu8 *)(tblBase + e * 44);
        vu16 *mp = *(vu16 **)(en + 8);
        if ((u32)mp < 0x02000000u || (u32)mp >= 0x03000000u) continue;
        vu16 *r0 = mp + MB_ROW * 32;            /* row20 */
        bool native = ((r0[13] & 0x3FF) == 0x01D &&
                       (mp[(MB_ROW + 1) * 32 + 13] & 0x3FF) == 0x023 &&
                       (mp[(MB_ROW + 2) * 32 + 13] & 0x3FF) == 0x02D);
        if (!native) continue;                  /* 已右移/貼中/空白態：不動 */
        bool shifted = ((r0[15] & 0x3FF) == 0x01D);
        if (shifted) continue;
        /* 欄B 判別：原生 space 右框列 0x1027 在 row21 tx26（v7.6.16 實錘）*/
        bool styleB = ((mp[(MB_ROW + 1) * 32 + 26] & 0x3FF) == 0x027);
        /* v7.6.26：用戶定案——space 鍵整體右移 2 列，花紋（鍵右框以右的
         * 裝飾帶 c27-31）完全不搬=自動裁掉其左側 2 列。三行統一搬 14 列
         * c13-26 → c15-28（鍵=左框 01D/023/02D@c13 + 體列 + 右框
         * 1027(row21)/1032(row22)@c26），c29-31 逐字節保持原生
         * （row21=[1C01@29][1003@30][0@31]：端線留原生 tx30、屏緣留白）。
         * 教訓鏈：v7.6.20-24 的 17 列整搬把花紋 1C04/1C01 也搬走（端線被擠
         * 出，回寫實驗 v7.6.22/23 全敗）；v7.6.25 的 row21 搬 11+row20/22
         * 搬 17 = 鍵右框三行撕裂（真機截圖：花紋覆蓋 space 格、右上缺一段）。
         * tile 貼圖數據從未改動，只動地圖條目排布。 */
        int cols = styleB ? 14 : 16;
        for (int r = 0; r < 3; r++) {
            vu16 *row = mp + (MB_ROW + r) * 32;
            for (int i = cols - 1; i >= 0; i--)
                row[MB_CELL6_TX + 2 + i] = row[MB_CELL6_TX + i];
        }
        /* 貼中 tx13-14（pal1，與 VRAM 側同款條目）*/
        r0[MB_CELL6_TX]           = (u16)(0x1000 | MB_ZH_TOPL);
        r0[MB_CELL6_TX + 1]       = (u16)(0x1000 | MB_ZH_TOPR);
        mp[(MB_ROW + 1) * 32 + MB_CELL6_TX]     = (u16)(0x1000 | MB_ZH_TILE_L);
        mp[(MB_ROW + 1) * 32 + MB_CELL6_TX + 1] = (u16)(0x1000 | MB_ZH_TILE_R);
        mp[(MB_ROW + 2) * 32 + MB_CELL6_TX]     = (u16)(0x1000 | MB_ZH_BOTL);
        mp[(MB_ROW + 2) * 32 + MB_CELL6_TX + 1] = (u16)(0x1000 | MB_ZH_BOTR);
        /* v7.6.26：花紋 c27-31 原生不動（詳見上註釋）。
         * MSRA v2=0xE=欄B（14 列鍵體右移）/0x10=欄A（16 列）。 */
        TtyLog("MSRA", e, styleB ? 0xEu : 0x10u);
    }
}
