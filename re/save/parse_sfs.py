#!/usr/bin/env python3
"""
Driver: decompress a Starfield .sfs, parse the documented body header + locate the FO4-style
file-location table, and scan for the trait-survey FormIDs.

Usage:
    python parse_sfs.py "path\\to\\Save.sfs" [body_out.bin]

This is a RESEARCH/READ-ONLY tool. It does not write into .sfs files. See
save-file-write-feasibility-2026-06-23.md for why writing is not pursued.
"""
import sys
import json
from sfs_container import BCPSContainer
from sfs_body import SFSBody
import scan_body


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    path = sys.argv[1]
    c = BCPSContainer(path)
    print("== CONTAINER ==")
    print(json.dumps(c.summary(), indent=2))
    body = c.decompress()
    print(f"\ndecompressed {len(body):,} bytes in {c.chunk_count} chunks "
          f"(expected {c.uncompressed_total:,})")

    if len(sys.argv) > 2:
        with open(sys.argv[2], "wb") as f:
            f.write(body)
        print("wrote body ->", sys.argv[2])

    print("\n== BODY (documented header + located file-location table) ==")
    b = SFSBody(body)
    s = b.summary()
    s["plugins_full_first8"] = [
        (p[:30] + "...") if len(p) > 30 else p for p in s["plugins_full_first8"]
    ]
    print(json.dumps(s, indent=2))

    print("\n== TRAIT FORMID SCAN ==")
    # reuse scan_body's logic by writing body to a temp and invoking, or call inline:
    tmp = (sys.argv[2] if len(sys.argv) > 2 else "_tmp_body.bin")
    if len(sys.argv) <= 2:
        with open(tmp, "wb") as f:
            f.write(body)
    sys.argv = ["scan_body", tmp]
    scan_body.main()


if __name__ == "__main__":
    main()
