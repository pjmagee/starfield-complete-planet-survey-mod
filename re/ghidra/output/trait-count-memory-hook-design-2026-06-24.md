# Trait scan-target "N/M SCANNED" — memory-hook design (2026-06-24)

Goal: make the in-world trait scan-target object display **M/M SCANNED** (complete) for traits we
have already marked known in the durable `938333` store, **without** writing the transient
`939118+0x28` byte (which jams the hand-scanner — see FINDINGS §4 regression note). This is the
**read-side detour** the previous "you can't write the count ref-free" conclusion
(`panel-count-source-2026-06-23.md` §5) did not consider.

## Why a hook, and why ID_90518 specifically

- The "N" digit = `model+0xa0` is written ONLY by **`ID_90518` @ `14159b890`** as
  `model+0xa0 = (uint)*(u8*)(ID_938422+0x38)` (the aim-bound scanner tally). `[decompile-verified]`
- **ID_90518 is SAFE to hook** (today's `hookfreq-xrefs-2026-06-24.txt`): its only callers are
  `ID_90676` and `ID_90661` — the paint/refresh path. It is NOT the per-frame count walker
  `ID_90521`←`ID_90501` (the one the Frida caveat says crashes when hooked). It fires on panel
  repaint, not every frame.

## The N write site (from `monocle-trait-count-asm.txt`)

```
14159ba17  MOV  RDI,[0x1461ea0c8]        ; RDI = ID_938422 (scanner-menu state)
14159ba2a  VMOVUPS [RBP+0x1a8] <- [RDI+0x10]   ; snapshot +0x10..0x30
14159ba7c  MOVZX EDI, byte [RBP+0x1d0]   ; EDI = ID_938422+0x38 = the N tally byte
14159ba83  LEA  RBX,[RSI+0x88]           ; RSI = the UI model; RBX = model+0x88
14159bab2  MOV  dword [RBX+0x18],EDI     ; *** model+0xa0 = N ***  (then setter [RAX+0x48] notify)
```
- `RSI` = the UI model object. `model+0xa0` = N (scanned), `model+0xc0` = M (total). M is NOT
  written in ID_90518 (sibling painter sets it), so at paint time `model+0xc0` already holds M.
- Params: `RCX`=param1 = **MonocleMenu** (`*param1 + 0xf18` = aimed-ref handle, see `14159b927`);
  `RDX`=param2 = model-factory ctx (`param2+0x578` virtual → the model RSI).

## The complication

The N write (`14159bab2`) happens BEFORE the trait/known determination. The durable `938333` read
is in **case-5** of the panel jump table (jump at `14159bc90` on `*(target+0xf2d)`), around:
```
14159c33e  MOV RCX,RDI ; CALL 0x1413079d0   ; ID_83009 canonical (RDI = aimed REFR)
14159c35f  CALL 0x1407bd600                 ; ID_52188 planet id
14159c3a3  MOV RCX,[RAX+0x8b0] ; CALL 0x14130aac0   ; knowledge DB read (ID_124900-family)
14159c3af  CMP byte [RBP+0x540],0x64        ; *** durable pct == 100 (discovered/complete)? ***
```
So a naive patch at the N-write can't gate on "known"; the known-state isn't computed yet.

## Recommended approach — mid-function detour at case-5 (`~14159c3af`)

At `14159c3af` the engine has just read OUR durable record (the same `938333` `MarkTraits` writes)
into `RBP+0x540` (pct, `0x64`==100) and `RSI` still points at the model. Piggyback on the engine's
own durable read — no separate known-check needed:

1. Trampoline-detour a ≥5-byte site at/after `14159c3af` (preserve ALL volatile regs + flags).
2. If `*(u8*)(RBP+0x540) == 0x64` (durable record says this trait is complete) AND `model+0xc0`
   (M) `> 0`: set `model+0xa0 = model+0xc0` and re-fire the model setter (`[model+0x88 vtbl +0x48]`)
   so the UI refreshes the "N" TUIValue. (Writing the raw dword without the setter won't repaint.)
3. Else fall through to the original behaviour.

Why this site: everything needed (model in RSI, durable-complete flag in RBP+0x540) is live; it
reflects `MarkTraits` automatically (engine reads the same store); and it's downstream of the
per-frame outline path, so no per-frame cost.

### Alternative (worse): entry detour + post-return fixup
Wrap ID_90518 at entry, call original, then re-resolve the model + re-derive known
(`ID_139363(*(param1)+0xf18)`→REFR→`ID_83009`/`ID_52188`/`ID_124900`) and force N=M. Rejected:
duplicates the engine's model + durable resolution (fragile), and needs the model pointer which the
post-return frame doesn't carry.

## Open items before writing the hook

1. **Re-decompile ID_90518 to C** (the `bejwvvrvo` dump is session-local, not in-repo) to confirm
   the exact case-5 offsets, that `RBP+0x540` is the durable pct, and the model setter vtable slot.
   Use `re/ghidra/scripts/DecompileIds.java` against the Ghidra project.
2. **Confirm `model+0xc0` == M** at paint time (read it live, or from the sibling "M" painter).
3. **Pick the 5-byte patch site** with stable register state; write the trampoline with CommonLibSF
   `SFSE::GetTrampoline().write_branch<5>()` + a hand-written asm thunk that saves/restores rax-r11
   + xmm + flags around the C callback.
4. **Crash-test in-game** (only the user can) — a fault here crashes the player. Gate behind a
   try/catch-free path is impossible mid-detour, so the callback must be allocation-free and
   pointer-checked. Start with a logging-only callback (no model write) to prove the site fires at
   the right time and `RBP+0x540`/model offsets read sane, THEN enable the write.

## Status

Design only. Hook NOT yet written. Prereq: step 1 (re-decompile) + step 4's logging-only probe.
The durable data + title-reveal already work via `MarkTraits`; this hook is purely the cosmetic
"N/M" digit. After fix-1 (the de-jam) the objects are also normally hand-scannable, so this is a
polish item, not a correctness fix.
