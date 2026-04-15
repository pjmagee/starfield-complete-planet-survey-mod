"""Flip the ESM (MASTER) flag on a .esp and rename it to .esm.

Replaces the xEdit workflow for plugins that CK saves as .esp but which we
ship as .esm. Keeps all other TES4 record flags intact; refuses to touch a
file that already has the 0x80 flag (reserved for official DLC — setting it
on hand-authored plugins crashes Starfield).

Usage:
    python esp_to_esm.py <path-to-plugin.esp>
"""

import struct
import sys
from pathlib import Path

MASTER_FLAG = 0x00000001
DLC_FLAG    = 0x00000080


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    esp = Path(sys.argv[1])
    if esp.suffix.lower() != ".esp":
        print(f"[FAIL] expected .esp, got {esp.suffix}")
        sys.exit(1)
    buf = bytearray(esp.read_bytes())
    if buf[0:4] != b"TES4":
        print("[FAIL] not a TES4 plugin (header magic mismatch)")
        sys.exit(1)
    flags = struct.unpack_from("<I", buf, 8)[0]
    if flags & DLC_FLAG:
        print(f"[FAIL] 0x80 DLC flag already set (flags=0x{flags:08X}); refusing to touch — would crash the game")
        sys.exit(1)
    new_flags = flags | MASTER_FLAG
    struct.pack_into("<I", buf, 8, new_flags)
    esm = esp.with_suffix(".esm")
    esm.write_bytes(bytes(buf))
    esp.unlink()
    print(f"[OK] flags 0x{flags:08X} -> 0x{new_flags:08X}, wrote {esm.name}, removed {esp.name}")


if __name__ == "__main__":
    main()
