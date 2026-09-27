"""
TWL 保留後處理：ndstool 重打包丟失了 arm9i/arm7i 與 modcrypt2 設定。
本腳本把原 ROM 的 TWL digest 區段（含 arm9i+arm7i）原樣附加到 ndstool
輸出的末尾，並修補 DSi 標頭中指向該區段的偏移，使 DSi 模式仍能載入 arm9i。
"""
import struct

BASE = "rom/base_rom.nds"          # 原始 ROM
NDS  = "rom/patched_rom.nds"      # ndstool 重打包輸出（無 arm9i）
OUT  = "rom/patched_twl.nds"      # 最終保留 TWL 的 ROM

# DSi 標頭欄位偏移（見 libnds tDSiHeader）
H_ARM9I_ROM   = 0x1C0
H_ARM9I_DEST  = 0x1C8
H_ARM9I_SIZE  = 0x1CC
H_ARM7I_ROM   = 0x1D0
H_ARM7I_DEST  = 0x1D8
H_ARM7I_SIZE  = 0x1DC
H_DIGEST_TWL_START = 0x1E8
H_DIGEST_TWL_SIZE  = 0x1EC
H_MODCRYPT2_START  = 0x220
H_MODCRYPT2_SIZE   = 0x224
H_ROMSIZE     = 0x80
H_TOTAL_ROM   = 0x208

base = bytearray(open(BASE, "rb").read())
nds  = bytearray(open(NDS,  "rb").read())

def g(d, o): return struct.unpack_from("<I", d, o)[0]
def s(d, o, v): struct.pack_into("<I", d, o, v)

arm9i_rom   = g(base, H_ARM9I_ROM)
arm9i_size  = g(base, H_ARM9I_SIZE)
arm7i_rom   = g(base, H_ARM7I_ROM)
arm7i_size  = g(base, H_ARM7I_SIZE)
digest_twl_start = g(base, H_DIGEST_TWL_START)
digest_twl_size  = g(base, H_DIGEST_TWL_SIZE)
modcrypt2_start  = g(base, H_MODCRYPT2_START)
modcrypt2_size   = g(base, H_MODCRYPT2_SIZE)

print("base arm9i rom=%s size=%s" % (hex(arm9i_rom), hex(arm9i_size)))
print("base arm7i rom=%s size=%s" % (hex(arm7i_rom), hex(arm7i_size)))
print("base digest_twl [%s, +%s)" % (hex(digest_twl_start), hex(digest_twl_size)))
print("base modcrypt2 start=%s size=%s" % (hex(modcrypt2_start), hex(modcrypt2_size)))

# 取出原 TWL digest 區段（arm9i 與 arm7i 都在其中）
twl_block = bytes(base[digest_twl_start : digest_twl_start + digest_twl_size])
assert len(twl_block) == digest_twl_size, "TWL 區段讀取不完整"

# arm7i 在 digest 區段內的相對偏移
arm7i_rel = arm7i_rom - digest_twl_start
print("arm7i relative offset in twl block = %s" % hex(arm7i_rel))

# 附加到 ndstool 輸出末尾（先對齊 0x200）
newbase = (len(nds) + 0x1FF) & ~0x1FF
if newbase != len(nds):
    nds.extend(b"\xFF" * (newbase - len(nds)))
out = nds + bytearray(twl_block)
out = bytearray(out)

# 修補 DSi 標頭偏移
s(out, H_ARM9I_ROM,        newbase)                 # arm9i ROM 偏移
s(out, H_ARM7I_ROM,        newbase + arm7i_rel)    # arm7i ROM 偏移
s(out, H_DIGEST_TWL_START, newbase)                 # digest_twl 區段起始
s(out, H_MODCRYPT2_START,  newbase)                 # modcrypt2 解密區段 = arm9i 起始
s(out, H_MODCRYPT2_SIZE,   modcrypt2_size)          # 還原 modcrypt2 長度
# RAM/size 欄位保持原值（arm9iDest/Size, arm7iDest/Size, digestTwlSize 不變）

# ROM 大小欄位
final_size = len(out)
s(out, H_ROMSIZE, final_size)
s(out, H_TOTAL_ROM, final_size)

open(OUT, "wb").write(out)
print("newbase=%s final_size=%s -> %s" % (hex(newbase), hex(final_size), OUT))

# 自檢
print("--- verify ---")
print("arm9iROMoff=%s (expect %s)" % (hex(g(out, H_ARM9I_ROM)), hex(newbase)))
print("arm7iROMoff=%s (expect %s)" % (hex(g(out, H_ARM7I_ROM)), hex(newbase + arm7i_rel)))
print("arm9iDest=%s size=%s" % (hex(g(out, H_ARM9I_DEST)), hex(g(out, H_ARM9I_SIZE))))
print("arm7iDest=%s size=%s" % (hex(g(out, H_ARM7I_DEST)), hex(g(out, H_ARM7I_SIZE))))
print("digestTwlStart=%s size=%s" % (hex(g(out, H_DIGEST_TWL_START)), hex(g(out, H_DIGEST_TWL_SIZE))))
print("modcrypt2Start=%s size=%s" % (hex(g(out, H_MODCRYPT2_START)), hex(g(out, H_MODCRYPT2_SIZE))))
print("romSize=%s totalRom=%s" % (hex(g(out, H_ROMSIZE)), hex(g(out, H_TOTAL_ROM))))
print("unitCode=%s" % hex(out[0x12]))
