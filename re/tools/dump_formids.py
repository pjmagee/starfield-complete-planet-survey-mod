"""Dump EditorID -> FormID for every record in a Starfield .esm/.esp plugin.

Usage:
    python dump_formids.py <path-to-plugin> [RECORD_TYPE ...]

With no record types, all records are printed. Examples:
    python dump_formids.py Data/CompletePlanetSurvey.esm
    python dump_formids.py Data/CompletePlanetSurvey.esm GPOF GPOG
"""

import struct
import sys
import zlib
from pathlib import Path

REC_HDR = 24          # Type(4) Size(4) Flags(4) FormID(4) Stamp(2) VCS(2) Version(2) Unk(2)
GRP_HDR = 24
SUB_HDR = 6           # Type(4) Size(2)
FLAG_COMPRESSED = 0x00040000


def read_subrecords(data: bytes):
    i = 0
    big_next_size = None
    while i + SUB_HDR <= len(data):
        sub_type = data[i:i + 4].decode("ascii", errors="replace")
        sub_size = struct.unpack_from("<H", data, i + 4)[0]
        i += SUB_HDR
        if sub_type == "XXXX":
            big_next_size = struct.unpack_from("<I", data, i)[0]
            i += sub_size
            continue
        if big_next_size is not None:
            sub_size = big_next_size
            big_next_size = None
        payload = data[i:i + sub_size]
        i += sub_size
        yield sub_type, payload


def walk(buf: bytes, offset: int, end: int, wanted: set, out: list):
    i = offset
    while i < end:
        tag = buf[i:i + 4].decode("ascii", errors="replace")
        if tag == "GRUP":
            grp_size = struct.unpack_from("<I", buf, i + 4)[0]
            walk(buf, i + GRP_HDR, i + grp_size, wanted, out)
            i += grp_size
            continue
        data_size, flags, form_id = struct.unpack_from("<III", buf, i + 4)
        rec_start = i + REC_HDR
        rec_end = rec_start + data_size
        if not wanted or tag in wanted:
            payload = buf[rec_start:rec_end]
            if flags & FLAG_COMPRESSED:
                decomp_size = struct.unpack_from("<I", payload, 0)[0]
                payload = zlib.decompress(payload[4:])
                assert len(payload) == decomp_size, "zlib size mismatch"
            edid = None
            for sub_type, sub_data in read_subrecords(payload):
                if sub_type == "EDID":
                    edid = sub_data.rstrip(b"\x00").decode("ascii", errors="replace")
                    break
            out.append((tag, form_id, edid))
        i = rec_end


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    path = Path(sys.argv[1])
    wanted = set(sys.argv[2:])
    buf = path.read_bytes()
    out: list = []
    walk(buf, 0, len(buf), wanted, out)
    for tag, form_id, edid in out:
        print(f"{tag}  0x{form_id:08X}  {edid or '<no EDID>'}")


if __name__ == "__main__":
    main()
