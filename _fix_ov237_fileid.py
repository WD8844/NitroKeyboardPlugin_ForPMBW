# -*- coding: utf-8 -*-
"""_fix_ov237_fileid.py — v7.9.28 ov237 文件 ID 修復（E1-E4 驗屍收官結論落實）

根因（2026-09-24 定案）：
  enler fork ndstool 把 added overlay（ov237）的 file_id 分配在 overlay 區末尾
  （=237），原版 nitrofs 文件全部 +1 錯位：原版 ID 237..483 的文件被頂到
  238..484（受害含 root 系統文件 child2_r.srl/child_r.srl/icon_b.w.char/
  .plt/utility.bin 與 49.15MB wb_sound_data.sdat）。遊戲/TWL 系統按硬編碼
  文件 ID 讀取 → 讀到錯文件 → 啟動內存計劃塌縮（8 個 Fnd 堆整組平移
  -0x50CCE0、原版 ~5.1MB 啟動分配缺失、0x03780000+ 3.8MB t4x4nflip 流式
  紋理緩存永不填充 → 街景流式直讀 SD → 3DS 掉幀）。

修復（數據零搬移；四階段全部 in-memory 後一次寫盤）：
  1) FAT 重排  new_FAT = old[0:237] + old[238:end] + old[237:238]
       => ID 0..236 原樣保留；237..483 = 原版 237..483（回歸原位）；
          484..486 = keyboard 三文件；487 = ov237（文件表末尾追加）
  2) OVT ov[237].file_id: 237 -> fat_count-1 (=487)，overlay_id 保持 237
  3) FNT 目錄表 first_file_id：>237 全部 -1（fork 隱式 ID 變體，文件項無顯式 id）
  4) TWL digest 重建：重算受影響 sector 的 HMAC-SHA1 → 受影響 block hash →
     master HMAC（算法/金鑰與 fork twlcrypto.cpp TwlFinalizeRom 逐行對應；
     受影響 sector 全部在 NTR 區明文段（idx>16，遠離 arm9 secure area
     加密段 0..15），無需 modcrypt/secure-area 加密表達）

關鍵性質：
  - 數據零搬移 => kmod RRESV797 注入的絕對偏移不變，無需重跑注入
  - header 0x0..0x15D 未動 => CRC16 不受影響；arm9/arm7/icon/arm9i/arm7i
    區域未動 => 對應 HMAC 不變
  - 舊 ov237 數據區成為無引用 padding，無害保留

冪等：ov[237].file_id 已 == fat_count-1 時視為已修復，僅做校驗。

用法（PYTHONUTF8=1）：
  python _fix_ov237_fileid.py --rom ROM/patched_twl.nds [--base ROM/base_rom.nds]
"""
import argparse
import hashlib
import hmac as hmac_mod
import os
import struct
import sys

OV_ID = 237  # added overlay 的 overlay_id（=_res_offset_inject.py OV237_ID）

# twlcrypto.cpp kHmacSha1Key（TWL HMAC-SHA1 專用金鑰）
HMAC_KEY = bytes([
    0x21, 0x06, 0xC0, 0xDE, 0xBA, 0x98, 0xCE, 0x3F, 0xA6, 0x92, 0xE3, 0x9D, 0x46, 0xF2, 0xED, 0x01,
    0x76, 0xE3, 0xCC, 0x08, 0x56, 0x23, 0x63, 0xFA, 0xCA, 0xD4, 0xEC, 0xDF, 0x9A, 0x62, 0x78, 0x34,
    0x8F, 0x6D, 0x63, 0x3C, 0xFE, 0x22, 0xCA, 0x92, 0x20, 0x88, 0x97, 0x23, 0xD2, 0xCF, 0xAE, 0xC2,
    0x32, 0x67, 0x8D, 0xFE, 0xCA, 0x83, 0x64, 0x98, 0xAC, 0xFD, 0x3E, 0x37, 0x87, 0x46, 0x58, 0x24,
])

# Header 欄位偏移（header.h struct Header 權威佈局）
OFF_DIGEST_NTR_START = 0x1E0
OFF_DIGEST_TWL_START = 0x1E8
OFF_SHT_START = 0x1F0
OFF_SHT_SIZE = 0x1F4
OFF_BHT_START = 0x1F8
OFF_BHT_SIZE = 0x1FC
OFF_SECTOR_SIZE = 0x200
OFF_BLOCK_SECTORCOUNT = 0x204
OFF_HMAC_DIGEST_MASTER = 0x328


def die(msg, code=1):
    print(f"ERROR: {msg}")
    sys.exit(code)


def thmac(data):
    """TWL HMAC-SHA1（= 標準 HMAC-SHA1，金鑰 kHmacSha1Key）。"""
    return hmac_mod.new(HMAC_KEY, data, hashlib.sha1).digest()


def load_layout(raw):
    fnto, fnts, fato, fats = struct.unpack_from("<4I", raw, 0x40)
    ovo, ovs = struct.unpack_from("<2I", raw, 0x50)
    return fnto, fnts, fato, fats, ovo, ovs


def ovt_entry(raw, ovo, idx):
    return struct.unpack_from("<8I", raw, ovo + idx * 32)


def rebuild_digests(raw, changed_addrs, ntr_only_plain=True):
    """重算 changed_addrs 涉及的 sector/block hash 與 master HMAC（就地改 raw）。

    前置校驗：先用未改動的明文 sector 抽樣驗證算法/金鑰/幾何無誤，
    再驗證現有 master 鏈，全部通過才動手。
    """
    ntr_o, ntr_s = struct.unpack_from("<2I", raw, OFF_DIGEST_NTR_START)
    twl_o, twl_s = struct.unpack_from("<2I", raw, OFF_DIGEST_TWL_START)
    sht_o, sht_s = struct.unpack_from("<2I", raw, OFF_SHT_START)
    bht_o, bht_s = struct.unpack_from("<2I", raw, OFF_BHT_START)
    ss = struct.unpack_from("<I", raw, OFF_SECTOR_SIZE)[0]
    bsc = struct.unpack_from("<I", raw, OFF_BLOCK_SECTORCOUNT)[0]

    def sector_index(addr):
        if ntr_o <= addr < ntr_o + ntr_s:
            return (addr - ntr_o) // ss
        if twl_o <= addr < twl_o + twl_s:
            return ntr_s // ss + (addr - twl_o) // ss
        return None

    ntr_count = ntr_s // ss
    digest_count = ntr_count + twl_s // ss

    # --- 收集受影響 sector ---
    changed_sectors = set()
    for a in changed_addrs:
        idx = sector_index(a)
        if idx is None:
            die(f"changed byte 0x{a:X} outside digest regions — cannot rebuild")
        changed_sectors.add(idx)
    # 全部必須落在 NTR 明文段（跳過前 0x4000=secure area 加密段 16 sector）
    if ntr_only_plain and any(i < 0x4000 // ss for i in changed_sectors):
        die("changed sector falls in secure-area digest segment — unsupported")

    # --- 抽樣校驗未改動明文 sector（驗證算法與幾何） ---
    sample = sorted({min(ntr_count - 1, 16 + (ntr_count // 3) * k) for k in (1, 2)}
                    | {digest_count - 1 if False else ntr_count - 2})
    for idx in sample:
        if idx in changed_sectors:
            continue
        off = ntr_o + idx * ss
        want = thmac(raw[off: off + ss])
        got = raw[sht_o + idx * 20: sht_o + idx * 20 + 20]
        if want != got:
            die(f"digest sanity FAIL: unchanged NTR sector {idx} hash mismatch — "
                f"algorithm/geometry wrong, refusing to patch")
    print(f"digest: 算法抽樣校驗 OK（sectors {sorted(sample)}）")

    # --- 校驗現有 master 鏈（patch 前基線） ---
    def master_of(table_bytes):
        block_input = bsc * 20
        block_count = (sht_s + block_input - 1) // block_input
        bh = bytearray(bht_s)
        for b in range(block_count):
            seg = table_bytes[b * block_input: (b + 1) * block_input]
            bh[b * 20: (b + 1) * 20] = thmac(seg)
        return thmac(bytes(bh))

    cur_table = bytes(raw[sht_o: sht_o + sht_s])
    cur_master = raw[OFF_HMAC_DIGEST_MASTER: OFF_HMAC_DIGEST_MASTER + 20]
    if master_of(cur_table) != cur_master:
        die("digest master chain mismatch BEFORE patch — refusing to patch")
    print("digest: 修改前 master 鏈驗證 OK")

    # --- 重算受影響 sector hash ---
    table = bytearray(cur_table)
    for idx in sorted(changed_sectors):
        off = ntr_o + idx * ss
        table[idx * 20: idx * 20 + 20] = thmac(raw[off: off + ss])

    # --- 重算受影響 block hash + master ---
    block_input = bsc * 20
    affected_blocks = sorted({idx // bsc for idx in changed_sectors})
    bh = bytearray(bht_s)
    block_count = (sht_s + block_input - 1) // block_input
    for b in range(block_count):
        seg = bytes(table[b * block_input: (b + 1) * block_input])
        bh[b * 20: (b + 1) * 20] = thmac(seg)
    raw[sht_o: sht_o + sht_s] = table
    raw[bht_o: bht_o + bht_s] = bh
    new_master = thmac(bytes(bh))
    raw[OFF_HMAC_DIGEST_MASTER: OFF_HMAC_DIGEST_MASTER + 20] = new_master
    print(f"digest: 重算 sector {sorted(changed_sectors)}、blocks {affected_blocks}、"
          f"master {new_master.hex()[:16]}… OK（sht@0x{sht_o:X} bht@0x{bht_o:X}）")


def fix(rom_path, base_path):
    with open(rom_path, "rb") as f:
        raw = bytearray(f.read())
    orig = bytes(raw)  # 修復前快照（用於校驗「僅預期字節改動」）
    fnto, fnts, fato, fats, ovo, ovs = load_layout(raw)
    fat_count = fats // 8

    oid, _ram, ram_sz, _bss, _bsz, _sinit, fid, _flags = ovt_entry(raw, ovo, OV_ID)
    if oid != OV_ID:
        die(f"OVT[{OV_ID}] overlay_id mismatch: got {oid}")
    if not (0 <= fid * 8 + 8 <= fats):
        die(f"OVT[{OV_ID}].file_id={fid} out of FAT range (count={fat_count})")

    did_fix = False
    changed_addrs = []
    if fid == fat_count - 1:
        print(f"already-fixed: OVT[{OV_ID}].file_id={fid} (== fat_count-1)，跳過改寫，僅校驗")
    elif fid == OV_ID:
        # --- 1) FAT 重排：old[0:F] + old[F+1:] + old[F] ---
        head = raw[fato: fato + fid * 8]
        ov237_slot = raw[fato + fid * 8: fato + (fid + 1) * 8]
        tail = raw[fato + (fid + 1) * 8: fato + fats]
        raw[fato: fato + fats] = head + tail + ov237_slot

        # --- 2) OVT file_id: F -> fat_count-1 ---
        struct.pack_into("<I", raw, ovo + OV_ID * 32 + 24, fat_count - 1)

        # --- 3) FNT first_file_id：>F 全部 -1 ---
        root_off = struct.unpack_from("<I", raw, fnto)[0]
        n_dirs = root_off // 8
        patched_dirs = 0
        for i in range(n_dirs):
            off, ffid, par = struct.unpack_from("<IHH", raw, fnto + i * 8)
            if ffid > fid:
                struct.pack_into("<H", raw, fnto + i * 8 + 4, ffid - 1)
                patched_dirs += 1
        print(f"fix: FAT 重排（F={fid} -> {fat_count - 1}）、"
              f"OVT[{OV_ID}].file_id {fid} -> {fat_count - 1}、"
              f"FNT {patched_dirs}/{n_dirs} 個 first_file_id -1")
        did_fix = True
        changed_addrs = [i for i in range(len(raw)) if raw[i] != orig[i]]

        # --- 4) TWL digest 重建（sector/block/master） ---
        rebuild_digests(raw, changed_addrs)

        if len(raw) != len(orig):
            die("size changed — abort")
        tmp = rom_path + ".tmp"
        with open(tmp, "wb") as f:
            f.write(raw)
        os.replace(tmp, rom_path)
        with open(rom_path, "rb") as f:
            raw = bytearray(f.read())
    else:
        die(f"OVT[{OV_ID}].file_id={fid} 既非 {OV_ID}（插入態）也非 fat_count-1"
            f"（已修復態）—— ROM 佈局與預期不符，拒絕處理")

    # ================= 校驗 =================
    fnto, fnts, fato, fats, ovo, ovs = load_layout(raw)
    fat_count = fats // 8
    ok = True

    # a) 改動字節僅限：FAT 區、OVT[OV_ID]+24、FNT 目錄表 first_file_id u16、
    #    digest 表區（sector/block/master）
    ntr_o, ntr_s = struct.unpack_from("<2I", raw, OFF_DIGEST_NTR_START)
    sht_o, sht_s = struct.unpack_from("<2I", raw, OFF_SHT_START)
    bht_o, bht_s = struct.unpack_from("<2I", raw, OFF_BHT_START)

    def allowed(addr):
        if fato <= addr < fato + fats:
            return True
        if ovo + OV_ID * 32 + 24 <= addr < ovo + OV_ID * 32 + 28:
            return True
        if fnto <= addr < fnto + 32 * 8 and (addr - fnto) % 8 in (4, 5):
            return True
        if did_fix and (sht_o <= addr < sht_o + sht_s or
                        bht_o <= addr < bht_o + bht_s or
                        OFF_HMAC_DIGEST_MASTER <= addr < OFF_HMAC_DIGEST_MASTER + 20):
            return True
        return False

    diffs = [i for i in range(len(raw)) if raw[i] != orig[i]]
    bad = [i for i in diffs if not allowed(i)]
    print(f"check: 修改字節數={len(diffs)}，越權字節={len(bad)}")
    if bad:
        ok = False
        print(f"  越權修改示例: {[hex(i) for i in bad[:8]]}")

    # b) OVT[OV_ID].file_id == fat_count-1；OVT 其餘條目不變
    oid, _ram, ram_sz, _bss, _bsz, _sinit, fid, _flags = ovt_entry(raw, ovo, OV_ID)
    if fid != fat_count - 1:
        ok = False
        print(f"check: FAIL OVT[{OV_ID}].file_id={fid} != {fat_count - 1}")
    else:
        print(f"check: OVT[{OV_ID}].file_id={fid}（overlay_id={oid} 保持）OK")

    # c) FAT 結構自洽（僅本次實際改寫時檢查）
    old_fat = orig[fato: fato + fats]
    new_fat = raw[fato: fato + fats]
    F = OV_ID
    if did_fix:
        if (new_fat[: F * 8] == old_fat[: F * 8] and
                new_fat[F * 8: (fat_count - 1) * 8] == old_fat[(F + 1) * 8:] and
                new_fat[(fat_count - 1) * 8:] == old_fat[F * 8: (F + 1) * 8]):
            print("check: FAT 重排逐字節符合 old[0:F]+old[F+1:]+old[F] OK")
        else:
            ok = False
            print("check: FAIL FAT 重排不自洽")

    # d) ov237 尺寸落位（已修復態用 OVT ram_sz 對照，兩種狀態皆成立）
    def sz(buf, i):
        s, e = struct.unpack_from("<II", buf, i * 8)
        return e - s
    new_last = sz(new_fat, fat_count - 1)
    print(f"check: ov237 ram_sz=0x{ram_sz:X} vs new[{fat_count - 1}]=0x{new_last:X} "
          f"{'OK' if ram_sz == new_last else 'FAIL'}")
    ok = ok and ram_sz == new_last

    # e) digest 自檢：master 鏈必須與當前 sector/block 表一致
    def master_of(table_bytes):
        bsc = struct.unpack_from("<I", raw, OFF_BLOCK_SECTORCOUNT)[0]
        block_input = bsc * 20
        block_count = (sht_s + block_input - 1) // block_input
        bh = bytearray(bht_s)
        for b in range(block_count):
            bh[b * 20: (b + 1) * 20] = thmac(
                table_bytes[b * block_input: (b + 1) * block_input])
        return thmac(bytes(bh))

    if master_of(bytes(raw[sht_o: sht_o + sht_s])) == \
            bytes(raw[OFF_HMAC_DIGEST_MASTER: OFF_HMAC_DIGEST_MASTER + 20]):
        print("check: digest master 鏈自洽 OK")
    else:
        ok = False
        print("check: FAIL digest master 鏈不自洽")

    # f) 與原版 ROM 對照：ID 0..(base_count-1) 尺寸序列全等（E1-E4 驗收標準）
    if base_path:
        with open(base_path, "rb") as f:
            base = f.read()
        _bf, _bfs, bfato, bfats = struct.unpack_from("<4I", base, 0x40)
        base_count = bfats // 8
        base_sizes = [sz(base[bfato: bfato + bfats], i) for i in range(base_count)]
        new_sizes = [sz(new_fat, i) for i in range(base_count)]
        if base_count <= fat_count and base_sizes == new_sizes:
            print(f"check: 原版 ID 0..{base_count - 1} 尺寸序列全等（{base_count}/{base_count}）OK —— 錯位已消除")
        else:
            ok = False
            mismatch = [i for i in range(min(base_count, fat_count))
                        if i < len(base_sizes) and i < len(new_sizes) and base_sizes[i] != new_sizes[i]]
            print(f"check: FAIL 原版尺寸序列不符，首個錯位 ID={mismatch[:5]}")

    print("RESULT: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=os.path.join("ROM", "patched_twl.nds"))
    ap.add_argument("--base", default=None, help="原版 ROM（尺寸序列終極校驗用）")
    args = ap.parse_args()
    if not os.path.exists(args.rom):
        print(f"ERROR: ROM not found: {args.rom}")
        sys.exit(2)
    sys.exit(fix(args.rom, args.base))


if __name__ == "__main__":
    main()
