# 宝可梦 黑/白 中文键盘插件

基于 [NitroKeyboardPlugin](https://github.com/enler/NitroKeyboardPlugin) 通用键盘插件框架，为《精灵宝可梦 黑/白》简体汉化版（ver.2.1.8）实例化的**游戏内中文输入键盘**。通过 overlay 钩点 + kmod 注入接入，不修改 SDK / bootstrap 代码。

在游戏的命名界面即可呼出中文键盘，用拼音输入宝可梦昵称与玩家名字。

## 功能（v7.9.31-E）

- **拼音输入**：全拼输入 + 候选词选择，候选区支持左右箭头翻页
- **十字键 + A/B 完整导航**：1.8 秒步进长按重复；↑ 在键盘顶行越界自动跳入候选区，↓ 出界返回键盘；A 上屏、B 退格
- **触摸 / 按键光标双向同步**：触摸键格时光标跟随移动，两种输入方式无缝混用
- **退出防误触**：退出键盘前等待抬笔，杜绝恢复游戏瞬间的原生键盘误输入
- **跨平台验证**：3DS（TWLM++ / nds-bootstrap）、DS 真机、DeSmuME / melonDS / no$gba 模拟器全部通过

## 快速上手：给 ROM 打补丁

**Windows（推荐）**：从 [Releases](../../releases) 下载对应版本的拖动补丁器，把未补丁的 .nds ROM 拖到 exe 图标上即可——补丁已内嵌，自动完成基础版本校验、打补丁、输出校验，零依赖。

**Linux / macOS**：

```bash
xdelta3 -d -s "未补丁原版.nds" "NitroKeyboardPlugin_v7.9.31E_BLACK.xdelta" "输出.nds"
```

**打补丁前务必核对基础 ROM MD5**（拖错版本会产出坏 ROM）：

| 版本 | 基础 ROM MD5 | 成品 ROM MD5 |
|---|---|---|
| 黑 | `43165862677bd3546d840b8cf1193962` | `d9a99c9cc7baadddadd079996f0c761e` |
| 白 | `0498307d30083815484eb63c439de6a0` | `784ebb2cccd64ad981efc73d7f1db454` |

完整教程（含模拟器注意事项、常见问题）：[docs/patch_tutorial.md](docs/patch_tutorial.md)（[HTML 版](docs/patch_tutorial.html)）

> ⚠️ 汉化 ROM 文本更新后旧补丁立即作废，请始终以发布页公布的 MD5 为准。

## 从源码构建

前置：devkitPro（libnds ≥ 2.0.0）、Python 3 + [script/requirements.txt](script/requirements.txt)、[enler/ndstool](https://github.com/enler/ndstool)（TWL 打包 + `--xdelta` 补丁生成）。

```bash
git clone --recurse-submodules https://github.com/enler/NitroKeyboardPlugin.git
```

- 变体切换：复制 `common/config.mk.example` 为 `common/config.mk`，`GAME_VARIANT` 设为 `BLACK` / `WHITE`（`ROM_DIR` 对应 `rom` / `rom_white`）
- 构建链十步（make → kmod 部署 → arm9/ov194/ov237 补丁 → TWL 打包 → 文件 ID 修复 → 偏移注入 → 校验）：见[打补丁教程第八节](docs/patch_tutorial.md)
- 适配原理与钩点定案：[docs/BW_KEYBOARD_ADAPTATION_GUIDE.md](docs/BW_KEYBOARD_ADAPTATION_GUIDE.md)
- 维护者：ROM 文本更新（a/0/0/2、a/0/0/3、字库 a/0/2/3）后重出补丁的完整清单，同见教程第八节

## 仓库结构

```
├── keyboard_module/            # 键盘主模块（kmod）：输入法、光标、拼音引擎
├── overlay/                    # 游戏侧 overlay 钩子（呼出/轮询/触摸/诊断）
├── overlay_ldr/                # overlay 加载器注入器
├── common/                     # 变体配置与精简 NitroSDK 接口层
│   ├── config.mk.example       # 变体/注入参数模板
│   └── symbols*.ld             # 黑/白版地址符号表
├── patch.py _patch_ov194.py _fix_ov237_fileid.py
├── _res_offset_inject.py       # BW 构建链：补丁/打包/文件 ID 修复/偏移注入
├── pack_twl.sh repack_twl.py
├── script/                     # 字体、码表、拼音词库生成工具
├── docs/                       # 打补丁教程、BW 适配指南
├── example/                    # 通用框架的其他游戏接入示例
└── third_party/                # rime-pinyin-simp 词库（git submodule）
```

## 免责声明

本项目为 NDS 游戏汉化社区的输入法工具，不包含、也不分发任何游戏 ROM 或其他受版权保护的游戏资源。使用本项目需自备相应游戏 ROM；请支持正版游戏。本项目与 Nintendo / The Pokémon Company 无关。

## 致谢

- 通用插件框架与构建工具：[NitroKeyboardPlugin](https://github.com/enler/NitroKeyboardPlugin) / [enler/ndstool](https://github.com/enler/ndstool)
- 默认拼音词库：[rime-pinyin-simp](https://github.com/rime/rime-pinyin-simp)（git submodule）
- 画面重绘实现参考：[nds-bootstrap](https://github.com/DS-Homebrew/nds-bootstrap) inGameMenu
