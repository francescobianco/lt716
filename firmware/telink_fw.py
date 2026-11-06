#!/usr/bin/env python3
"""Turn a raw tc32 objcopy binary into a Telink OTA image, like the SDK's tl_check_fw:
set the size field at 0x18 to the final length and append ~crc32 (little-endian)."""
import sys
import zlib

src, dst = sys.argv[1], sys.argv[2]
b = bytearray(open(src, "rb").read())
if b[8:12] != b"KNLT":
    sys.exit(f"{src}: no KNLT magic at +8")
# like tl_check_fw: pad with FF to a multiple of 16, then append the CRC. The SDK OTA code
# only accepts a size field with (size & 0xf) == 4.
b += b"\xff" * (-len(b) % 16)
size = len(b) + 4
# bytes 6-7 = FLAG_FW_CHECK 0x5d, FW_CHECK_AGTHM2 0x02, as in the stock images: the receiving
# OTA code then verifies the CRC32 trailer before switching to the new image
b[6], b[7] = 0x5D, 0x02
b[0x18:0x1C] = size.to_bytes(4, "little")
crc = (zlib.crc32(bytes(b)) ^ 0xFFFFFFFF) & 0xFFFFFFFF
b += crc.to_bytes(4, "little")
open(dst, "wb").write(b)
print(f"{dst}: {size} bytes, crc32 {crc:#010x}")
