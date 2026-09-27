import os
import re
import shutil

from script.patch_util import PatchUtil

def read_config_mk(file_path):
    config = {}
    pattern = r'([A-Z_]+)\s*:=\s*(.+)$'
    with open(file_path, 'r') as f:
        for line in f:
            line = line.strip()
            if line:
                match = re.match(pattern, line)
                if match:
                    key, value = match.groups()
                    if value.startswith('0x'):
                        config[key] = int(value, 16)
                    elif value.isdigit():
                        config[key] = int(value)
                    else:
                        config[key] = value.strip()
    return config


# SDK5 的 FS_LoadOverlayInfo 会校验 overlay_id 是否小于上限（当前=237）。
# 经反汇编确认，BW(汉化版) 的上限字面量存放在运行地址 0x02078DB4
# （FS_LoadOverlayInfo=0x02078C40 内的 ldr r0,[pc,#344] 字面池）。
# 把它放宽到 OVERLAY_ID+1，使我们追加的 overlay 237 能被加载。
# 白版（2026-09-13 偵察定案）：arm9 代碼全域 +0x18，FS_LoadOverlayInfo
# =0x02078C58，上限字面量 @0x02078DCC（值=237 已驗證）。
FS_OVERLAY_ID_LIMIT_ADDR = {
    'BLACK': 0x02078DB4,
    'WHITE': 0x02078DCC,
}

if __name__ == '__main__':
    config_mk_path = 'common/config.mk'
    config = read_config_mk(config_mk_path)
    overlay_id = config['OVERLAY_ID']
    overlay_elf = config['OVERLAY_NAME'] + '.elf'
    overlay_bin = config['OVERLAY_NAME'] + '.bin'
    overlay_addr = config['OVERLAY_ADDR']
    overlay_ldr_addr = config['OVERLAY_LDR_ADDR']
    inject_overlay_id = config['INJECT_OVERLAY_ID']
    is_nitrosdk_thumb = config.get('IS_NITROSDK_THUMB', 0)
    game_variant = config.get('GAME_VARIANT', 'BLACK')
    rom_dir = config.get('ROM_DIR', 'rom')
    fs_limit_addr = FS_OVERLAY_ID_LIMIT_ADDR[game_variant]
    base_rom_path = os.path.join(rom_dir, 'base_rom.nds')
    overlay_ldr_elf = 'overlay_ldr.elf'
    overlay_ldr_bin = 'overlay_ldr.bin'

    print(f"GAME_VARIANT={game_variant} ROM_DIR={rom_dir} FS_LIMIT={fs_limit_addr:#x}")
    print(f"OVERLAY_ID={overlay_id} ADDR={hex(overlay_addr)} LDR_ADDR={hex(overlay_ldr_addr)} INJECT={inject_overlay_id} THUMB={is_nitrosdk_thumb}")

    patch_util = PatchUtil(base_rom_path)
    # 1. 在 overlay_table 中加入我们的键盘宿主 overlay
    patch_util.add_overlay_entry(overlay_elf, overlay_id, overlay_addr)
    # 2. 把第一个载入的 overlay(0) 的静态初始化函数改成 overlay_ldr 的入口
    patch_util.modify_overlay_init_functions(inject_overlay_id, overlay_ldr_elf)
    # 3. 移动 NDS 模式 arena 指针，腾出键盘插件空间
    patch_util.modify_arena_lo(overlay_elf, overlay_addr)
    # 4. 移动 TWL(NDSi) 模式 arena 指针（BW 为 NDSi 增强游戏，需同时改）
    patch_util.modify_arena_lo(overlay_elf, overlay_addr, patch_twl=True)
    # 5. 注入 overlay_ldr 到 arm9 的安全区空闲位置
    patch_util.inject_overlay_ldr(overlay_ldr_bin, overlay_ldr_addr)
    # 6. 把 overlay_ldr 的 bss 作为 autoload section 附加到 arm9
    patch_util.add_bss_as_autoload_section(overlay_ldr_elf)
    # ndspy 对新增 section 的 initFuncTable 默认为 None，TWL 保存路径要求整数
    for s in patch_util.codefile.sections:
        if getattr(s, 'initFuncTable', 0) is None:
            s.initFuncTable = 0
    # 7. SDK5: 放宽 FS_LoadOverlayInfo 的 overlay_id 上限
    patch_util.patch_word(fs_limit_addr, overlay_id + 1)
    # 8. 保存
    patch_util.save_arm9_binary(os.path.join(rom_dir, 'arm9.bin'))
    patch_util.save_overlay_table(os.path.join(rom_dir, 'overlay_table.bin'))

    dest_dir = os.path.join(rom_dir, 'overlay')
    shutil.copy2(overlay_bin, os.path.join(dest_dir, os.path.basename(overlay_bin)))
    print("patch done: %s/arm9.bin, %s/overlay_table.bin, %s/overlay/%s" % (rom_dir, rom_dir, rom_dir, overlay_bin))
