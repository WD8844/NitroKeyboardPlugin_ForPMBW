# 寶可夢黑（ver2.1.8漢化）鍵盤插件適配指南

## 當前狀態（已完成）
- 鍵盤宿主 overlay 已載入並可 R+X 呼出（不崩潰）
- 載入鏈：FS_LoadOverlay(0,10) → overlay_ldr → FS_LoadOverlay(0,237) → keyboard.kmod
- **未完成**：10 個遊戲專屬介面函數均為 stub，無實際輸入功能

## 待逆向的 10 個介面（教程第4節）

### 1. Alloc / Free（內存分配）— 已用 64KB 靜態數組臨時解決
- 當前：`static u8 heap[64*1024]`，返回固定數組
- 正式做法：逆向確認黑版用哪個分配器
  - `OS_AllocFromHeap(0x020867EC)` / `OS_FreeToHeap(0x020868F4)` — NitroSDK
  - `FndAllocFromExpHeapEx(0x02060BAC)` / `FndFreeToHeap(0x02060D3C)` — NitroSystem
- no$gba 斷點：在這兩個函數下斷，看遊戲分配內存時的參數（heap handle/id）
- BW 通常用 NitroSystem，heap handle 是動態的，要逆向取得

### 2. OnOverlayLoaded — 初始化 hook
- 當前：空函數
- 需做：hook 遊戲的「命名界面進入/退出」函數，取得命名上下文變量
- 逆向方法：
  - 在遊戲裡進入「給寶可夢取名」界面
  - 內存搜索輸入的文字（如"12345"）定位名字字符串地址
  - 對該地址下寫斷點，輸入新文字時觸發，從那裡逆向找命名函數
  - hook 命名開始/結束函數，保存上下文指針

### 3. ShouldShowKeyboard — 已實現（R+X）
- 當前：`KEY_PRESSED(KEY_R | KEY_X)`
- 可改進：結合命名界面狀態變量，只在命名時才彈出

### 4. GetMaxInputLength — 返回最大字符數
- 當前：返回 0（需修正）
- 逆向：BW 給寶可夢取名 = 5 字符（10字節），給盒子重命名 = 8 字符
- 建議間接獲取（從命名上下文結構體讀），而非硬編碼

### 5. GetInitialInputText（可選）— 初始文本
- 讓鍵盤彈出時帶上已有名字
- 從命名上下文讀取當前名字字符串指針

### 6. LoadGlyph — 字模獲取（關鍵）
- 當前：返回 false（鍵盤無字顯示）
- BW 字庫：`nitrofs/a/0/0/2`（6.4MB NARC，含 NFTR 字體）
- 兩種實現方式：
  a. **讀 ROM 字庫文件**：用 FS_OpenFile 讀 a/0/0/2，解析 NFTR，轉換為 16x16 2bpp
  b. **調用遊戲函數**：逆向遊戲渲染文字的函數，直接調用取得字模
- 字模格式：16x16 2bpp 低位在前（教程要求）
- 推薦先用方式 a，參考 DQ5 範例（讀內存中的 NFTR 字庫）

### 7. KeycodeToChar — 編碼轉換
- 當前：返回 false
- 需確認：BW 漢化版用什麼編碼存中文
  - 原版 BW：UTF-16
  - 漢化版：可能改為自訂雙字節編碼或保留 UTF-16
- 確認方法：內存搜索中文字串，看字節模式
  - UTF-16：`FF FE` 開頭或每字 2 字節
  - 自訂編碼：需建轉換表（用 `create_keycode_conv_table.py` 生成）
- 若為 UTF-16：直接 `*output = keycode` 即可

### 8. CanContinueInput — 繼續輸入判斷
- 雙字節編碼：直接返回 true
- 單雙混合：需判斷是否越界（BW 應為純雙字節，返回 true 即可）

### 9. OnInputFinished — 輸入完成回調（關鍵）
- 當前：空函數
- 需做：把 inputText 複製到遊戲命名內存，觸發畫面更新
- 逆向：
  - 定位名字字符串在內存的地址（從命名上下文取得）
  - 複製 inputText 到該地址
  - 調用遊戲的畫面更新函數（或設置 flag）

## 逆向工具推薦

| 工具 | 用途 |
|---|---|
| **no$gba Debugger** | 斷點、內存搜索、暫存器查看（你已在用）|
| **Tinke** | NARC 解包、字庫視覺化查看（C# 寫的 NDS ROM 工具）|
| **CrystalTile2 (CT2)** | ROM 編輯、字庫查看、vb2bpp 格式預覽 |
| **DeSmuME** | 另一個調試器，內存搜索更方便（但 DSi 模式支援弱）|
| **HxD** | Windows 十六進位編輯器，快速查看內存 dump |

## no$gba 逆向工作流（建議順序）

### 第一步：確認編碼（30分鐘）
1. 進遊戲到「給寶可夢取名」界面
2. 輸入幾個英文字母如 "ABCD"
3. no$gba 內存搜索 "ABCD" 的 UTF-16 表示 `41 00 42 00 43 00 44 00`
4. 找到後，輸入中文字（若漢化版支持），看內存字節模式
5. 確認是 UTF-16 還是自訂編碼

### 第二步：定位命名上下文（1-2小時）
1. 內存搜索名字字符串地址
2. 對該地址下寫斷點
3. 修改名字時觸發斷點，看調用棧
4. 逆向找到命名界面進入/退出函數
5. 找到保存命名上下文的變量地址

### 第三步：實現 OnOverlayLoaded + OnInputFinished（2-3小時）
1. hook 命名進入函數，保存上下文指針到全局變量
2. 在 OnInputFinished 中：從上下文取得名字地址，複製 inputText
3. 觸發畫面更新

### 第四步：實現 LoadGlyph（3-5小時）
1. 用 Tinke 解包 `a/0/0/2` NARC
2. 找到 NFTR 字庫文件
3. 解析 NFTR 結構（參考 common/lib/src/nitro/font.c 的 NitroFontInfoSection）
4. 實現 LoadGlyph：charCode → 查 NFTR → 轉 16x16 2bpp
5. 或逆向遊戲渲染函數直接調用

### 第五步：實現 KeycodeToChar + GetMaxInputLength（1小時）
- 若 UTF-16：KeycodeToChar 直接賦值
- GetMaxInputLength：從命名上下文讀或硬編碼 5

## 關鍵內存地址（已確認）

| 符號 | 地址 | 用途 |
|---|---|---|
| FS_LoadOverlay | 0x02079170 | 載入 overlay |
| FS_OpenFile | 0x02078B24 | 開啟文件 |
| FS_ReadFile | 0x02078A7C | 讀文件 |
| OS_AllocFromHeap | 0x020867EC | SDK 分配器 |
| FndAllocFromExpHeapEx | 0x02060BAC | NitroSystem 分配器 |
| LanucherThreadContext | 0x02150E5C | 線程上下文（鍵盤用）|
| HW_BUTTON_XY_BUF | 0x02FFFFA8 | 按鍵狀態 |
| TP_GetCalibratedPoint | 0x0208B010 | 觸摸點 |

## 字庫文件位置
- `nitrofs/a/0/0/2` — 6.4MB NARC，BW 主字庫（含 NFTR）
- `nitrofs/gfl_font.dat` — 2.3KB，小圖示字庫（非主字庫）
- `nitrofs/dl_rom/icon_b.char` / `icon_w.char` — 下載圖示

## 範例參考
- **DQ5**（`example/dragon_quest_5`）：讀內存 NFTR 字庫 + UTF-8 編碼，最接近 BW 的場景
- **寶可夢心金**（`example/pokemon_hg`）：直接讀 ROM 字庫文件，同為寶可夢系列
- **寶可夢信長**（`example/pokemon_conquest`）：NDSi 增強 + SDK5，同為 SDK5 遊戲

## 編譯與測試命令
```bash
# 編譯
/d/devkitpro/msys2/usr/bin/env MSYSTEM=MSYS /d/devkitpro/msys2/usr/bin/bash -lc 'source /etc/profile.d/devkit-env.sh; cd <插件仓库目录> && make'

# 打補丁 + 重打包
python3 patch.py && cd rom && ndstool -c patched_rom.nds -9 arm9.bin -7 arm7.bin -d nitrofs -t banner.bin -h header.bin -y9 overlay_table.bin -y overlay && cd .. && python3 repack_twl.py && ndstool -f rom/patched_twl.nds
```
