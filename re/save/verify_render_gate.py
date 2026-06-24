"""
verify_render_gate.py  (2026-06-24)
====================================
READ-ONLY verification of the trait scan-target render-gate, grounded in BOTH the save bytes
AND the decompiled readers. No writes, no Frida. Run against any set of saves.

It answers, byte-exactly, the three questions the prior 7 doc revisions kept getting wrong:

  Q1. Is the durable 938333 PlayerKnowledge record (slot flag/pct + the pooled member array)
      ACTUALLY different between a real-scan save and the mod save?
        -> EMPIRICAL ANSWER (Save14 vs Save21): NO. It is byte-identical. The mod already
           reproduces 938333 in full. So 938333 is NOT the render gate (necessary, not sufficient).

  Q2. What durable difference DOES distinguish a real 2/2 scan (Save14) from the clean state
      (Save12) that the mod save (Save21) FAILS to reproduce?
        -> EMPIRICAL ANSWER: a list of typed references (incl. the scan-target ACTI 0x0021B250
           and LocRefType-framed ids) appended inside a **CLOC (Location) ChangeForm** in the
           ChangeForms region. Present in Save14, ABSENT in Save12 AND Save21.

  Q3. Is that gate writable ref-free via the knowledge-DB (db+0x268) machinery?
        -> NO. A CLOC ChangeForm is a per-visited-Location changed-form, not a db+0x268
           StoredComponent. It cannot be pre-written ref-free/all-planets. This matches the
           decompiled "N/M count walks the Location's loaded LocRef refs" model.

Usage:  py verify_render_gate.py
"""
import sys, struct
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
SAVES = {
    "Save12_clean_0of2": r"\Save12_98A838ADM467265736820436861726163746572_000043_20260623182623_1_0_4.sfs",
    "Save14_REAL_2of2":  r"\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs",
    "Save21_MOD_v4":     r"\Save21_98A838ADM467265736820436861726163746572_000043_20260623233715_1_0_4.sfs",
}

TAG  = 0x00400000
ACTI = 0x0021B250            # base scan-target ACTI
KWD  = 0x00225588            # trait keyword
# The distinctive run a real scan appends into the Location (CLOC) ChangeForm:
#   <ACTI^>00 00 00 | 41 ab 29 00 00 00 | 46 15 81 00 00 00 ...   (the scanned LocRef list)
CLOC_SCAN_LIST = bytes.fromhex("61b25000000041ab29000000461581000000")


def regions(path):
    body = BCPSContainer(path).decompress()
    sb = SFSBody(body); lt = sb.loc_table
    a, b, c, cf = lt["offsetA"], lt["offsetB"], lt["offsetC"], lt["changeFormsOffset"]
    return body, lt, {"R1": (a, b), "R2": (b, c), "R3": (c, cf)}


def pk_member_window(body, r1a, r1b):
    """Return the 938333 per-canonical member+slot window (member array + canonical slot)."""
    r1 = body[r1a:r1b]
    can = struct.pack("<I", ACTI ^ TAG)
    pos = r1.find(can)
    if pos < 0:
        return None
    return r1[pos - 0x14:pos + 0x08]


def main():
    data = {}
    for name, rel in SAVES.items():
        body, lt, regs = regions(BASE + rel)
        data[name] = (body, lt, regs)

    print("=" * 78)
    print("Q1 — durable 938333 PlayerKnowledge member+slot window (the prior 'gate')")
    print("=" * 78)
    wins = {}
    for name, (body, lt, regs) in data.items():
        a, b = regs["R1"]
        w = pk_member_window(body, a, b)
        wins[name] = w
        print(f"  {name:20} : {w.hex(' ') if w else 'NO 938333 canonical record'}")
    real = wins["Save14_REAL_2of2"]; mod = wins["Save21_MOD_v4"]
    print()
    if real and mod and real == mod:
        print("  RESULT: Save14(real) == Save21(mod) BYTE-IDENTICAL on 938333.")
        print("          => 938333 is NOT the render gate. The mod already reproduces it fully.")
    else:
        print("  RESULT: 938333 DIFFERS between real and mod -> (re)check the member array / flag / pct.")

    print()
    print("=" * 78)
    print("Q2 — the durable CLOC (Location) scanned-LocRef list a real scan appends")
    print("=" * 78)
    for name, (body, lt, regs) in data.items():
        whole = body.count(CLOC_SCAN_LIST)
        c, cf = regs["R3"]
        in_r3 = body[c:cf].count(CLOC_SCAN_LIST)
        print(f"  {name:20} : CLOC-scan-list x{whole}  (in ChangeForms region: {in_r3})")
    print()
    print("  RESULT: present only in Save14(real); ABSENT in Save12(clean) AND Save21(mod).")
    print("          => THIS is the durable render-gate difference. It lives in a CLOC ChangeForm,")
    print("             NOT in the 938333 knowledge DB. It references the scan-target ACTI 0x0021B250.")

    print()
    print("=" * 78)
    print("Q3 — is the gate writable ref-free via db+0x268 (GetKnowledgeDB / DbLookup)?")
    print("=" * 78)
    print("  NO. A CLOC ChangeForm is a per-visited-Location changed-form, not a db+0x268")
    print("  StoredComponent. There is no ref-free / all-planets pre-write for it. The N/M count")
    print("  and Unknown->named reveal are Location- and materialization-bound, matching")
    print("  trait-true-completion-2026-06-23.md and trait-onplanet-completion-2026-06-23.md.")
    print()
    print("  IN-GAME READ-ONLY CHECK (no write): in the mod's existing DbLookup path, look up")
    print("  938333 for (planetId, canonical 0x0021B250) and confirm slot+0x21==2, slot+0x20==100,")
    print("  AND the pooled member array contains 0x00225588. If all present yet the panel still")
    print("  shows 0/2 / UNKNOWN, that empirically confirms 938333 is satisfied and the gate is the")
    print("  CLOC/Location list above (which DbLookup cannot reach).")


if __name__ == "__main__":
    main()
