"""Clear the ESM (MASTER) flag on a .esm and rename it to .esp.

Inverse of esp_to_esm.py. Use when you need to re-open the plugin in
Creation Kit for editing — CK only saves .esp files, so development
rounds-trip via .esp.

Usage:
    python esm_to_esp.py <path-to-plugin.esm>
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
    esm = Path(sys.argv[1])
    if esm.suffix.lower() != ".esm":
        print(f"[FAIL] expected .esm, got {esm.suffix}")
        sys.exit(1)
    buf = bytearray(esm.read_bytes())
    if buf[0:4] != b"TES4":
        print("[FAIL] not a TES4 plugin (header magic mismatch)")
        sys.exit(1)
    flags = struct.unpack_from("<I", buf, 8)[0]
    if flags & DLC_FLAG:
        print(f"[FAIL] 0x80 DLC flag set (flags=0x{flags:08X}); refusing to touch")
        sys.exit(1)
    new_flags = flags & ~MASTER_FLAG
    struct.pack_into("<I", buf, 8, new_flags)
    esp = esm.with_suffix(".esp")
    esp.write_bytes(bytes(buf))
    esm.unlink()
    print(f"[OK] flags 0x{flags:08X} -> 0x{new_flags:08X}, wrote {esp.name}, removed {esm.name}")


if __name__ == "__main__":
    main()
