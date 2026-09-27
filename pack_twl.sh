#pack_twl.sh — v7.6.22 起改用 enler/ndstool 一步打包 DSi/TWL 相容 ROM
#（舊鏈 ndstool -c + repack_twl.py + ndstool -f 已退役：舊法只搬原版
#  digest 區段、無法重建 digest/HMAC；新法完整重建且原版級校驗全過）
#用法: bash pack_twl.sh <ndstool.exe 路徑>   （在 msys2 bash 中執行）
set -e
NDS="${1:-${NDS_TOOL:-ndstool}}"   # 也可用环境变量 NDS_TOOL 指定 ndstool 路径
ROOT="$(cd "$(dirname "$0")" && pwd)"
ROMDIR="${ROM_DIR:-$ROOT/rom}"   # 可用 ROM_DIR=rom_white 切換白版
TWL="$ROMDIR/twl"
mkdir -p "$TWL"

# 1) 從原版 ROM 提取明文 arm9i/arm7i（fork 支援 -9i/-7i，自動解 modcrypt、驗 HMAC）
#    提取一次後存檔，之後重建可跳過（arm9i.bin 開頭應為 ARM 指令而非隨機數）
if [ ! -f "$TWL/arm9i.bin" ]; then
  "$NDS" -x "$ROMDIR/base_rom.nds" -9i "$TWL/arm9i.bin" -7i "$TWL/arm7i.bin"
fi

# 2) overlay 目錄改名為 fork 的 canonical 命名 overlay9_XXXX.bin（硬連結不佔空間）
mkdir -p "$ROMDIR/overlay9"
for f in "$ROMDIR"/overlay/overlay_*.bin; do
  b=$(basename "$f"); id=${b#overlay_}; id=${id%.bin}
  t="$ROMDIR/overlay9/overlay9_$(printf '%04d' $((10#$id))).bin"
  [ -f "$t" ] || ln "$f" "$t" 2>/dev/null || cp "$f" "$t"
done

# 3) 一步重建：NTR 組件（補丁後）+ TWL 頭模板(0x1000) + 明文 arm9i/arm7i
#    fork 自動重壓 arm9i 並重建 digest 表/HMAC/modcrypt
"$NDS" -c "$ROMDIR/patched_twl.nds" \
  -9 "$ROMDIR/arm9.bin" -7 "$ROMDIR/arm7.bin" \
  -9i "$TWL/arm9i.bin" -7i "$TWL/arm7i.bin" \
  -d "$ROMDIR/nitrofs" -t "$ROMDIR/banner.bin" -h "$ROMDIR/header.bin" \
  -y9 "$ROMDIR/overlay_table.bin" -y "$ROMDIR/overlay9"

# 4) 校驗（應與原版同為全 OK；Segment3 CRC INVALID 為零售卡常態）
"$NDS" -v -i "$ROMDIR/patched_twl.nds" | grep -E "HMAC|Digest|Segment3"
echo "PACK DONE: $ROMDIR/patched_twl.nds"
