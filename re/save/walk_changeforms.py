"""
Walk the Starfield ChangeForms section as a sequence of records, to LOCATE the single
record that a scan adds (count 15362->15363) / edits in place (15363->15363).

Starfield ChangeForm record header (empirically; FO4/SSE-derived, Starfield variant):
    formID      : u32  (packed reference, top bits = type selector)
    changeFlags : u32
    type        : u8
    version     : u8
    lengthSize  : encoded in (type >> 6): 0->u8, 1->u16, 2->u32 length fields
    length      : (lengthSize bytes)  uncompressed length
    [length2]   : (lengthSize bytes)  present when changeFlags has the "compressed" bit? — we test
    data        : length bytes

We do NOT trust a fixed header; we PROBE several header encodings against the section and pick the
one that walks cleanly to exactly changeFormCount records ending at the section end. Then we diff the
record streams across the three saves by (formID) to find the added/edited record.
"""
import sys
import struct

sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
SAVES = {
    "0of2": BASE + r"\Save12_98A838ADM467265736820436861726163746572_000043_20260623182623_1_0_4.sfs",
    "1of2": BASE + r"\Save13_98A838ADM467265736820436861726163746572_000043_20260623182638_1_0_4.sfs",
    "2of2": BASE + r"\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs",
}


def load(path):
    body = BCPSContainer(path).decompress()
    sb = SFSBody(body)
    base = sb.loc_table["table_base"]
    cf = sb.loc_table["changeFormsOffset"]
    fid_off = struct.unpack_from("<I", body, base - 8)[0]
    return body, cf, fid_off, sb.loc_table["changeFormCount"], base


def walk(body, cf, cf_end, want_count):
    """Try the Starfield CF record encoding. Header is:
       u32 formId, u32 changeFlags, u8 type, u8 ver, then length by (type>>6).
       Length appears TWICE when bit? — we try single-length first.
       Returns (records, ok). records = list of (off, formId, flags, type, ver, dlen, hdrlen)."""
    # The CF section begins with a small preamble before the first record. Observed:
    #   u32 0x3e8(=1000) ... then u16 list. We must find where records begin.
    # Empirically scan a window of plausible starts and pick the one that walks to want_count.
    best = None
    for ls_mode in (0, 1, 2):           # length field size: u8/u16/u32
        for dbl in (False, True):        # length stored once or twice
            for start_probe in range(0, 0x40):
                recs = walk_from(body, cf + start_probe, cf_end, ls_mode, dbl)
                if recs is None:
                    continue
                n, end = recs
                if abs(n - want_count) <= 1 and end <= cf_end and end >= cf_end - 8:
                    return (cf + start_probe, ls_mode, dbl, n, end)
                if best is None or (abs(n - want_count) < abs(best[3] - want_count)):
                    best = (cf + start_probe, ls_mode, dbl, n, end)
    return best


def lenfield(body, p, ls_mode):
    if ls_mode == 0:
        return body[p], p + 1
    if ls_mode == 1:
        return struct.unpack_from("<H", body, p)[0], p + 2
    return struct.unpack_from("<I", body, p)[0], p + 4


def walk_from(body, start, cf_end, ls_mode, dbl):
    p = start
    n = 0
    while p + 10 <= cf_end:
        formId = struct.unpack_from("<I", body, p)[0]
        flags = struct.unpack_from("<I", body, p + 4)[0]
        typ = body[p + 8]
        ver = body[p + 9]
        q = p + 10
        dlen, q = lenfield(body, q, ls_mode)
        if dbl:
            _ul, q = lenfield(body, q, ls_mode)
        nxt = q + dlen
        if nxt > cf_end or dlen > 0x200000:
            return (n, p)  # walk broke here
        p = nxt
        n += 1
        if n > 20000:
            return (n, p)
    return (n, p)


def main():
    for k, path in SAVES.items():
        body, cf, cf_end, cfc, base = load(path)
        res = walk(body, cf, cf_end, cfc)
        print(f"{k}: cf=0x{cf:X} end=0x{cf_end:X} count={cfc}")
        print(f"   best walk: start=0x{res[0]:X} ls_mode={res[1]} dbl={res[2]} "
              f"records={res[3]} endsAt=0x{res[4]:X} (target end 0x{cf_end:X})")


if __name__ == "__main__":
    main()
