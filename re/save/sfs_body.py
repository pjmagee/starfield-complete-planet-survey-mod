"""
Starfield decompressed save body (SFS_SAVEGAME) parser.

DOCUMENTED layout (per Nexus-Mods/StarfieldSaveTool README): only the header + plugin
info are reverse-engineered. Everything AFTER pluginInfo -- form-id arrays, ChangeForms,
GlobalData -- is officially "not worked out yet". This module parses the documented part
exactly, then offers heuristic probing of the undocumented remainder (string/formid scans).

All ints little-endian. Strings here are wstring = u16 length-prefixed (NOT NUL terminated
in the count, but the bytes shown include a trailing structure; we read exactly `len` bytes).
Some fields use bstring (u16 len + bytes). We follow the README field order.
"""
import struct
import sys


class Reader:
    def __init__(self, buf, pos=0):
        self.b = buf
        self.p = pos

    def u8(self):
        v = self.b[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from("<H", self.b, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.p)[0]; self.p += 4; return v

    def u64(self):
        v = struct.unpack_from("<Q", self.b, self.p)[0]; self.p += 8; return v

    def f32(self):
        v = struct.unpack_from("<f", self.b, self.p)[0]; self.p += 4; return v

    def wstr(self):
        n = self.u16()
        s = self.b[self.p:self.p + n]; self.p += n
        return s.decode("utf-8", "replace")

    def raw(self, n):
        s = self.b[self.p:self.p + n]; self.p += n
        return s


class SFSBody:
    def __init__(self, body):
        if body[:12] != b"SFS_SAVEGAME":
            raise ValueError("not an SFS_SAVEGAME body")
        self.body = body
        self.r = Reader(body, 12)
        self._parse()

    def _parse(self):
        r = self.r
        self.header_size = r.u32()
        hdr_start = r.p
        self.header_bytes = r.raw(self.header_size)
        # plugin info + version strings follow the header block
        self.save_version_byte = r.u8()
        self.cur_game_version = r.wstr()
        self.created_game_version = r.wstr()
        self.plugin_info_size = r.u16()
        plugin_start = r.p
        self._parse_plugins(plugin_start)
        # The FO4/SSE-style FILE LOCATION TABLE follows the plugin block. Its absolute
        # offset varies, so locate it generically: 4 ascending section offsets all < body
        # length, followed by the three small global-data-table COUNTS (observed 17/21/9),
        # then changeFormCount, 0, formIDArrayOffset, 0.
        self.loc_table = self._find_location_table()
        self.remainder_start = plugin_start + self.plugin_info_size

    def _find_location_table(self):
        b = self.body
        bl = len(b)
        for base in range(0x1000, 0x8000):
            o = struct.unpack_from("<6I", b, base)
            if o[0] < o[1] < o[2] < o[3] < bl and 0 < o[0] and o[4] < 1000 and o[5] < 1000:
                nxt = struct.unpack_from("<5I", b, base + 24)
                # nxt = [gdt3Count, changeFormCount, zero, formIDArrayOffset, zero]
                return {
                    "table_base": base,
                    "offsetA": o[0],            # data region start (just past table)
                    "offsetB": o[1],            # global data table 2 region
                    "offsetC": o[2],            # global data table 1 region
                    "changeFormsOffset": o[3],  # change forms region
                    "gdt1Count": o[4],          # 17
                    "gdt2Count": o[5],          # 21
                    "gdt3Count": nxt[0],        # 9
                    "changeFormCount": nxt[1],
                    "formIDArrayOffset": nxt[3],
                    "_offsets_are_relative_to": "table_base (FO4 convention); "
                    "Starfield record encoding inside each section is NOT FO4-compatible "
                    "(see save-file-write-feasibility writeup)",
                }
        return None

    def _parse_plugins(self, start):
        # NOTE: the per-plugin extraInfo encoding for saveVersion>=140 (creation name/id,
        # variable-length flag block, achievementCompatible byte) is only partially
        # documented; our walk reliably reads the FULL master names (the part we need to
        # confirm CompletePlanetSurvey.esm load order) but may mis-step on extraInfo.
        # We DO NOT depend on this walk for section offsets -- those come from
        # _find_location_table(), which scans structurally. The walk is capped so a
        # mis-parse cannot run away.
        r = Reader(self.body, start)
        self.plugins = []
        end_cap = min(len(self.body), start + self.plugin_info_size + 0x20000)
        try:
            unk0 = r.u8()
            unk1 = r.u8()
            full_count = r.u8()
            for _ in range(min(full_count, 256)):
                if r.p >= end_cap:
                    break
                name = r.wstr()
                if ".es" not in name:  # stop if we've fallen out of the name stream
                    break
                self.plugins.append(("FULL", name))
            self.plugin_unk = (unk0, unk1)
        except Exception as e:
            self.plugin_parse_error = str(e)

    def summary(self):
        return {
            "header_size": self.header_size,
            "save_version_byte": self.save_version_byte,
            "cur_game_version": self.cur_game_version,
            "created_game_version": self.created_game_version,
            "plugin_info_size": self.plugin_info_size,
            "plugin_count": len(self.plugins),
            "plugins_full_first8": [n for t, n in self.plugins if t == "FULL"][:8],
            "remainder_start": hex(self.remainder_start),
            "remainder_len": len(self.body) - self.remainder_start,
            "body_len": len(self.body),
            "location_table": self.loc_table,
        }


if __name__ == "__main__":
    body = open(sys.argv[1], "rb").read()
    b = SFSBody(body)
    import json
    print(json.dumps(b.summary(), indent=2))
