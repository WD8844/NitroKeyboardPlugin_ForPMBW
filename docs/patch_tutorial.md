# 宝可梦 黑/白 中文键盘插件 · 打补丁教程

> NitroKeyboardPlugin v7.9.31-E —— 黑/白双版通用教程
> 把未补丁的原版 ROM 打上补丁，即可获得带中文键盘插件（拼音输入候选、十字键导航、触摸同步光标）的成品 ROM。

---

## 一、准备材料

| 项目 | 说明 |
|---|---|
| 未补丁的原版 ROM（.nds） | 黑版 / 白版各需对应版本，见第二节 MD5 校验 |
| 补丁文件 | `NitroKeyboardPlugin_v7.9.31E_BLACK.xdelta`（黑）/ `NitroKeyboardPlugin_v7.9.31E_WHITE.xdelta`（白） |
| Windows 拖动版（可选） | `NitroKeyboardPlugin_BLACK_拖我打补丁.exe` / `NitroKeyboardPlugin_WHITE_拖我打补丁.exe` |

**黑 / 白补丁互不通用**，请确认手上的 ROM 和补丁是同一版本。

---

## 二、核对基础 ROM（重要！）

补丁只对应**特定的未补丁原版**。打补丁前请先核对 ROM 的 MD5：

| 版本 | 基础 ROM MD5（必须一致） |
|---|---|
| 宝可梦 黑 | `43165862677bd3546d840b8cf1193962` |
| 宝可梦 白 | `0498307d30083815484eb63c439de6a0` |

**查看 MD5 的命令：**

- Windows（命令提示符 / PowerShell）：
  ```
  certutil -hashfile "ROM文件名.nds" MD5
  ```
- Linux：
  ```
  md5sum "ROM文件名.nds"
  ```
- macOS：
  ```
  md5 -q "ROM文件名.nds"
  ```

> ⚠️ 如果 MD5 不一致，说明 ROM 版本不对（可能是已补丁版、别的汉化版或别的 dump），强行打补丁会失败或产出坏 ROM。

---

## 三、Windows：拖动打补丁（推荐，零依赖）

1. 下载对应版本的 `拖我打补丁.exe`（黑版拖黑、白版拖白）。
2. **把未补丁的 .nds ROM 直接拖到 exe 图标上**，松手。
3. 程序自动完成三步：
   - 校验输入 ROM（版本不对会明确报错并提示需要的 MD5）；
   - 打补丁（补丁已内嵌在 exe 里，**无需安装 xdelta3 或任何其他工具**）；
   - 校验输出 ROM 的 MD5，通过才提示"打补丁成功"。
4. 输出文件自动命名为 `<原文件名>+NKP.nds`，与原 ROM 在同一目录。

任何失败都会自动删除半成品文件，不会留下坏 ROM。

<details>
<summary>（可选）Windows 命令行方式</summary>

如果你习惯命令行且已有 `xdelta3.exe`：

```
xdelta3 -d -s "原版ROM.nds" "NitroKeyboardPlugin_v7.9.31E_BLACK.xdelta" "输出.nds"
```

</details>

---

## 四、Linux：命令行打补丁

### 1. 安装 xdelta3

```
# Debian / Ubuntu
sudo apt install xdelta3

# Fedora
sudo dnf install xdelta3

# Arch Linux
sudo pacman -S xdelta3
```

### 2. 打补丁

```
# 黑版
xdelta3 -d -s "精灵宝可梦 黑_ver.2.1.8.nds" "NitroKeyboardPlugin_v7.9.31E_BLACK.xdelta" "精灵宝可梦 黑_ver.2.1.8+NKP.nds"

# 白版
xdelta3 -d -s "精灵宝可梦 白_ver.2.1.8.nds" "NitroKeyboardPlugin_v7.9.31E_WHITE.xdelta" "精灵宝可梦 白_ver.2.1.8+NKP.nds"
```

成功时命令安静结束，不输出内容；报错说明基础 ROM 版本不对（见第七节）。

---

## 五、macOS：命令行打补丁

### 1. 安装 xdelta3（需 Homebrew）

```
brew install xdelta3
```

### 2. 打补丁（命令与 Linux 相同）

```
# 黑版
xdelta3 -d -s "精灵宝可梦 黑_ver.2.1.8.nds" "NitroKeyboardPlugin_v7.9.31E_BLACK.xdelta" "精灵宝可梦 黑_ver.2.1.8+NKP.nds"

# 白版
xdelta3 -d -s "精灵宝可梦 白_ver.2.1.8.nds" "NitroKeyboardPlugin_v7.9.31E_WHITE.xdelta" "精灵宝可梦 白_ver.2.1.8+NKP.nds"
```

---

## 六、验证输出（打完必做）

打补丁完成后的 ROM，MD5 应当**完全等于**以下值：

| 版本 | 成品 ROM MD5 |
|---|---|
| 黑版成品 | `d9a99c9cc7baadddadd079996f0c761e` |
| 白版成品 | `784ebb2cccd64ad981efc73d7f1db454` |

核对命令同第二节。MD5 一致 = 与我们发布时上机测试通过的 ROM 逐字节一致，可直接使用。

---

## 七、常见问题

**Q：xdelta3 报错（如 `source file too small` / `target window checksum mismatch` / `invalid input`）？**
A：基础 ROM 版本不对。请回到第二节核对 MD5——最常见的情形是用了已补丁版 ROM，或黑/白搞反了。

**Q：Windows 拖动 exe 提示"输入 ROM 与本补丁的基础版本不符"？**
A：同上，黑白拖反或 ROM 版本不对。按提示里的 MD5 核对。

**Q：3DS（TWLM++）用户注意**
A：更换/重打 ROM 后第一次启动前，请删除 SD 卡上的：
```
_nds/nds-bootstrap/patchOffsetCache（文件名含 ROM 名的 .bin）
```
否则旧的 offset 缓存会导致启动异常。

**Q：模拟器 / 真机支持？**
A：输出 ROM 可直接用于 3DS（TWLM++ / nds-bootstrap）、DS 真机（DSi / 烧录卡）以及 DeSmuME、melonDS、no$gba 等模拟器。

**Q：汉化 ROM 更新了文本（错别字修正、字库修订）之后，我之前下载的补丁还能用吗？**
A：**不能**。基础 ROM 任何字节变化都会使旧补丁作废（基础 MD5 不再匹配）。请以发布页最新公布的补丁文件与 MD5 为准——拖动 exe 会自动校验，命令行用户务必核对第二节的基础 MD5，否则可能静默产出坏 ROM。

---

## 八、维护者附录：ROM 更新后重走整条构建链

> 本节面向插件/汉化维护者。每次修正文本（a/0/0/2、a/0/0/3）或重做字库（a/0/2/3）并重新打包后，按此清单重新出补丁。

### 1. 触发条件与兼容性速查

| 你的改动 | 插件/构建链是否兼容 | 需要做什么 |
|---|---|---|
| a/0/0/2、a/0/0/3 文本修订（尺寸变化 → NitroFS 重排） | ✅ 自动适配 | 重跑构建链即可，注入偏移由 `_res_offset_inject.py` 自动重算 |
| 用 Tinke/ndstool 重新打包 | ✅ 自动适配 | 链条重新提取+打包，文件 ID / TWL digest 由 `_fix_ov237_fileid.py` 自动重建 |
| 重做 a/0/2/3 字库（新增码表字符） | ⚠️ 有两个硬耦合点 | 见下方"人工检查点" |
| 更新 arm9 / overlay 代码 | ❌ 钩点全失效 | 等同于新基线，钩点分析全部重做 |

### 2. 人工检查点（构建前）

- **NFTR 容器偏移**：kmod 直接读 a/0/2/3 容器内的 NFTR 字形，**容器内偏移 0x74 是硬编码**。重做字库后先核对 NFTR magic 在容器内的起始偏移是否仍为 0x74；变了就同步修改 kmod 常量并重编。
- **码表索引**：pinyin_db.bin 的候选字按码表索引引用。新增字符请**尾部追加**（既有索引不变）；若调整了既有字符顺序，拼音候选会选错字。字库更新后做一次键盘候选回归（打几个常用拼音核对候选）。
- **白版同步**：`config.mk` 切 `WHITE`/`rom_white`，三组件必须 `make -B` 全量重建；完成后切回 `BLACK`/`rom`。

### 3. 构建链（顺序铁律）

```
① 新汉化版 ROM → 覆盖 rom/base_rom.nds（白版 rom_white/base_rom.nds）
② make（msys2 bash -lc；env DEVKITPRO/DEVKITARM/TMP 绝对路径）
   ⚠️ rtdbg.h 有改动时必须 make -B 全量重建
③ cp keyboard.kmod → rom/nitrofs/keyboard/（md5 核对）
④ patch.py（PYTHONUTF8=1 + 托管 Py3.13 + PYTHONPATH=nitro_pylibs）
⑤ _patch_ov194.py
⑥ pack_twl.sh（一段；内建第 3.5 步 fileid fix 不可移除/不可跳过）
⑦ _res_offset_inject.py patch（自动重算 kmod/keys/pinyin 注入偏移）
⑧ pack_twl.sh（二段）
⑨ verify（Git Bash 跑托管 python）：fileid fix / digest / HMAC 全 PASS、
   注入 verify OK、kmod 与 ov237 in-ROM byte-exact
⑩ cp 测试目录 —— 交付以测试目录文件的 md5 为准
```

⚠️ 红线：不可跑根目录 `make clean`（会误删取证 dump）；bash -lc 后每条 python/cp 必须显式 cd。

### 4. 发布 SOP（构建通过后）

```
① 重新生成补丁：
   ndstool --xdelta --verify rom/base_rom.nds rom/patched_twl.nds patches/…_BLACK.xdelta
   ndstool --xdelta --verify rom_white/base_rom.nds rom_white/patched_twl.nds patches/…_WHITE.xdelta
   （--verify 必须 PASS，另用 xdelta3 CLI 回环核对输出 MD5）
② build_drop_patcher.sh 更新 4 个 MD5（黑/白 base + 黑/白成品）→ 重出两个拖动 exe
③ 同步更新本教程（第二节/第六节）与 使用说明.txt 里的全部 MD5
④ 3DS 测试前删除 _nds/nds-bootstrap/patchOffsetCache
```

### 5. 版本标识建议

补丁文件名带上基础版标识（如 `_base2.1.8a_` 或基础 MD5 前 8 位）。ROM 每更新一轮，旧补丁必须下架或明确标注作废——否则用户拖错补丁时，xdelta3 命令行**可能不报错而静默产出坏 ROM**，这是线上反馈的高发事故源。

---

## 附：本补丁包含的内容

- 全套中文键盘：拼音输入 + 候选词选择（支持翻页）
- 十字键 + A/B 完整键盘导航（1.8 秒步进、↑ 出界跳入候选区、↓ 返回键盘）
- 触摸 / 按键光标双向同步
- 触摸退出防误触（抬笔确认）
