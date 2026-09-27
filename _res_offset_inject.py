# -*- coding: utf-8 -*-
"""_res_offset_inject.py — v7.9.7 零 FS 化資源偏移注入

兩段式流程（鐵律：TWL digest 由 ndstool_enler 打包時重建，嚴禁打包後直改成品內容）：
  1) bash pack_twl.sh                      # 第一次打包：確定 FAT 佈局
  2) python _res_offset_inject.py patch    # 解析成品 FAT → 補丁鬆散 ROM/overlay9/overlay9_0237.bin
  3) bash pack_twl.sh                      # 第二次打包：文件大小未變 → FAT 佈局逐字節一致
  4) python _res_offset_inject.py verify   # 校驗成品內 ov237 注入值

解析方式（2026-09-18 定案）：
  - file-id 用 ndspy 的 filenames 映射（name→id；本 ROM 的 FNT 為 fork 定製
    變體，文件項不含顯式 id，手寫 FNT 樹解析不可行）
  - ROM 偏移用原始 FAT 表直讀（FAT 是標準 8B 項：start,end）
  - ov237 的 file-id 用原始 ARM9 overlay 表（頭 0x50，項 32B，file_id@+0x18；
    flags=0 未壓縮）
  - ov237 數據區掃描 'RRESV797' 魔數（gRomResTable.magic，rom_res.c），
    其後 8 個 u32 即注入槽：offset[4] + size[4]
  - 只改文件內容、不改大小 → 頭部 CRC16（僅覆蓋頭 512B）與 FAT 佈局均不受影響

用法（PYTHONUTF8=1，PYTHONPATH=nitro_pylibs）：
  python _res_offset_inject.py patch   [--rom ROM/patched_twl.nds]
  python _res_offset_inject.py verify  [--rom ROM/patched_twl.nds]
"""
import argparse
import os
import struct
import sys

# 依赖 ndspy（pip install -r script/requirements.txt）

MAGIC = b"RRESV797"

# RomRes id 對應 nitrofs 路徑（順序=ROMRES_ID_KMOD/KEYSTEX/PINYIN/GAMEFONT）
RES_PATHS = [
    "keyboard/keyboard.kmod",
    "keyboard/keys.tex",
    "keyboard/pinyin_db.bin",
    "a/0/2/3",
]
OV237_ID = 237


def read_fat_offsets(raw, file_ids):
    fat_off = struct.unpack_from("<I", raw, 0x48)[0]
    out = []
    for fid in file_ids:
        s, e = struct.unpack_from("<II", raw, fat_off + fid * 8)
        if s == 0 and e == 0:
            raise RuntimeError(f"FAT[{fid}] empty")
        out.append((s, e))
    return out


def resolve(raw):
    """返回 (offsets[4], sizes[4], ov_start, ov_end)。"""
    import ndspy.rom as rom_mod
    rom = rom_mod.NintendoDSRom.fromFile(args_rom)  # filenames 解析需要 ndspy 對象
    ids = []
    for p in RES_PATHS:
        ids.append(rom.filenames[p])
    entries = read_fat_offsets(raw, ids)
    offsets = [e[0] for e in entries]
    sizes = [e[1] - e[0] for e in entries]

    ov_off, ov_size = struct.unpack_from("<II", raw, 0x50)
    oid, _ram, _ramsz, _bss, _s1, _s2, fid, flags = struct.unpack_from(
        "<8I", raw, ov_off + OV237_ID * 32)
    if oid != OV237_ID:
        raise RuntimeError(f"overlay table[{OV237_ID}] id mismatch: {oid}")
    if flags & 1:
        raise RuntimeError("ov237 is compressed in ROM — injection plan invalid")
    s, e = read_fat_offsets(raw, [fid])[0]
    return offsets, sizes, s, e


def table_bytes(offsets, sizes):
    return struct.pack("<8I", *(offsets + sizes))


def main():
    global args_rom
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["patch", "verify"])
    ap.add_argument("--rom", default=os.path.join("ROM", "patched_twl.nds"))
    args = ap.parse_args()
    args_rom = args.rom
    # v7.9.17：rom 目錄從 --rom 推導（白/黑變體自動正確）。
    # 舊版此處硬編碼 "ROM"——Windows 大小寫不敏感→永遠落在黑版 rom/，
    # 白版注入會污染黑版鬆散 ov237（v7.9.16 白版遷移時實錘）。
    rom_dir = os.path.dirname(os.path.abspath(args.rom))

    if not os.path.exists(args.rom):
        print(f"ERROR: ROM not found: {args.rom}")
        sys.exit(2)
    with open(args.rom, "rb") as f:
        raw = f.read()

    offsets, sizes, ov_start, ov_end = resolve(raw)
    print("resource table:")
    for i, p in enumerate(RES_PATHS):
        print(f"  [{i}] {p:28s} off={offsets[i]:#010X} size={sizes[i]:#X}")
    print(f"ov237 file: off={ov_start:#010X} size={ov_end - ov_start:#X}")

    if args.mode == "patch":
        # 補丁鬆散 overlay 文件（與成品內同字節內容）
        loose = os.path.join(rom_dir, "overlay9", f"overlay9_{OV237_ID:04d}.bin")
        if not os.path.exists(loose):
            print(f"ERROR: loose overlay not found: {loose}")
            sys.exit(2)
        with open(loose, "rb") as f:
            ov = bytearray(f.read())
        idx = ov.find(MAGIC)
        if idx < 0:
            print("ERROR: RRESV797 magic not found in loose ov237 — rebuild ov237 first")
            sys.exit(3)
        if ov.find(MAGIC, idx + 1) >= 0:
            print("ERROR: magic not unique in loose ov237")
            sys.exit(3)
        tb = table_bytes(offsets, sizes)
        old = bytes(ov[idx + 8: idx + 8 + 32])
        if old == tb:
            print("patch: table already up to date")
        else:
            ov[idx + 8: idx + 8 + 32] = tb
            if len(ov) != len(open(loose, "rb").read()):
                print("ERROR: size changed — abort")
                sys.exit(5)
            with open(loose, "wb") as f:
                f.write(ov)
            print(f"patch: wrote 8 u32 into {loose} @+{idx + 8:#x}")
            print("NEXT: re-run pack_twl.sh (layout identical), then verify")
    else:
        idx = raw.find(MAGIC, ov_start, ov_end)
        if idx < 0:
            print("verify: FAIL — magic not in ov237 region of final ROM")
            sys.exit(4)
        want = table_bytes(offsets, sizes)
        got = raw[idx + 8: idx + 8 + 32]
        if got == want:
            print("verify: OK — 成品 ROM 內注入值與 FAT 解析值一致")
        else:
            go, wo = struct.unpack("<8I", got), struct.unpack("<8I", want)
            for i, (g, w) in enumerate(zip(go, wo)):
                mark = "" if g == w else "  <-- MISMATCH"
                print(f"  slot{i}: rom={g:#010X} want={w:#010X}{mark}")
            print("verify: FAIL — 成品內表值不符（是否漏了第二次打包？）")
            sys.exit(4)


if __name__ == "__main__":
    main()
