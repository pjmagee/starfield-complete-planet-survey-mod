# Fauna func-699 ability/resistance/weakness markers — offline derivation (2026-06-23)

**Goal.** Resolve, purely from `Starfield.esm`, whether an NPC_ "has a magic effect
with keyword K" for the 3 HandScanner scanner keywords, so the func-699
(`HasMagicEffectKeyword`) catalog markers can be derived offline:

| catalog idx | marker | scanner keyword (CTDA param1) |
|---|---|---|
| 13 | `0x002634BF` HandScannerActorAbilities   | `0x001D3B47` HandScannerActorAbilityEffectKeyword |
| 12 | `0x002634C1` HandScannerActorResistances | `0x001D3B48` HandScannerActorResistanceEffectKeyword |
| 14 | `0x002634C0` HandScannerActorWeaknesses  | `0x001D3B46` HandScannerActorWeaknessEffectKeyword |

(Confirmed in fauna catalog FLST `0x00160C97`: each is `op=0x00 func=699 comp=1
param1=<scanner kw>`, CITC=1.)

---

## KEY DISTINCTION (resolves the apparent GT conflict)

There are TWO different marker surfaces, and func 699 belongs only to the second:

1. **Per-SPECIES slot+0x08 (the green set).** What the engine's `DumpSpeciesSlots`
   produces and what the mod writes. A bare species has **no live actor**, so
   `HasMagicEffectKeyword` is evaluated false → the func-699 markers are NOT in this
   set. This is exactly why the 9 Jemison GT fauna are "exactly 4 markers" and why
   the existing 17/17 validation holds. **This path is unchanged.**

2. **Live in-game HandScanner attributes (per spawned creature).** The scanner
   evaluates func 699 against the actual actor's active magic effects, adding the
   extra "Abilities / Resistances / Weaknesses" attributes (e.g. "Abilities:
   Venomous"). This is what we now derive offline from the species' STATIC ability
   attachments.

The earlier model's note "func 699 = display/perk-gated, never in green set" is
correct *for surface 1* and stays correct. The fix adds surface 2 as a separate,
opt-in `actor_scan=True` path.

---

## THE DECODED CHAIN (NPC_ → scanner-keyword MGEF), with byte citations

```
NPC_  OBTS  →  OMOD  →  (DATA 'NPRK')  →  PERK  →  (PRKE type 1 + DATA)  →  SPEL  →  (EFID)  →  MGEF  →  (KWDA) scanner kw
```

### 1. NPC_ OBTS (object-template) — 7-byte OMOD entries
Verified layout (same as the validated temperament chain):
`u32 entryCount @0` ; 18-byte prefix `@4..0x11` ; then `entryCount × 7-byte entries`
from `@0x12`, each = `u32 OMOD formid` + `3 bytes 00 01 01`.

### 2. OMOD DATA property table — the `NPRK` tag adds a PERK
OMOD DATA is a property table (same table that carries `NKEY` for temperament). Each
property entry ends with `… 04000000 02000000 <TAG4> <value u32>` where `02000000` is
the value-type word (2 = formid) and `04000000` is the value length.
Example — `mod_CCT_Attack_Poison` (OMOD `0x00256152`), DATA hex tail:
```
…04000000 02000000 4e50524b 905d2500…
              type     'NPRK'   perk=0x00255D90
```
`'NPRK'` (4e 50 52 4b) at DATA offset 0x38; the perk formid is the u32 immediately
after the tag. (`'NKEY'` 4e 4b 45 59 uses the identical encoding — that's the existing
validated temperament path.)

### 3. PERK — only **type-1 (Ability) entries** make the creature HOLD the MGEF
PERK record: `PRKE` (entry header; byte 0 = entry type: 1=Ability, 2=EntryPoint) then
the entry's `DATA`. For a type-1 entry, `DATA`'s first u32 is the SPEL added to the
**perk owner** (the creature itself). Example — `CCT_HitSpell_Poison_Perk`
(`0x00255D90`): entry types `[1, 2]`; the type-1 entry's DATA →
SPEL `0x00169210 CCT_Scan_Attack_Poison_P`.

**Why type-1 only:** the type-2 EntryPoint ("Apply Combat Hit Spell") applies a spell
to the creature's TARGET on hit, so the creature does not itself hold that MGEF and
`HasMagicEffectKeyword` would be false for it. Every CCT scanner perk pairs a type-1
self-Ability entry with the type-2 hit entry — the type-1 self-ability is what the
scanner reads. (All 20 scanner-referencing perks were verified to carry a type-1
ability entry.)

### 4. SPEL EFID → MGEF; MGEF KWDA → scanner keyword
SPEL `EFID` subrecord = `u32 MGEF formid` per effect. The MGEF's `KWDA` array
(`u32 keyword formid × n`) is checked for membership in the 3 scanner keywords.
Example completing the venom chain:
```
SPEL 0x00169210 CCT_Scan_Attack_Poison_P
  EFID → MGEF 0x00169220 CCT_Scan_Attack_Poison
                 KWDA ∋ 0x001D3B47 (AbilityEffectKeyword) → marker 0x002634BF Abilities
```
34 MGEFs carry a scanner keyword; 27 SPELs reach one via EFID; 20 PERKs add such a
SPEL via a type-1 entry.

### Other attachment paths investigated (and ruled out)
- **NPC_ direct SPLO (spell list):** 0 NPCs reference a scanner spell directly.
- **NPC_ direct PKID (perk list):** 0 NPCs reference a scanner perk directly.
- **RACE spell/perk lists:** 0 RACEs reference a scanner spell/perk.
- **TPLT/leveled templates:** the abilities on the base PCM_* creatures resolve fully
  via OBTS→OMOD→NPRK with no unresolved indirection. Pure leveled-list (`Lvl…`) NPCs
  that only inherit via TPLT would not be resolvable offline, but the surveyable base
  creatures carry their OMODs directly. **No offline-unresolvable limitation observed
  for the surveyable fauna.**

So OBTS → OMOD(NPRK) → PERK(type1) → SPEL → MGEF is the **sole** path, and it is fully
offline-resolvable.

---

## CODE (added to `re/tools/esm_derive_markers.py`)

`EsmDB.__init__` now also builds (one pass each):
- `mgef_scan_kw`  : MGEF formid → {scanner kws in its KWDA}
- `spel_scan_kw`  : SPEL formid → {scanner kws via its EFID→MGEF}
- `perk_ability_spel` : PERK formid → [type-1 Ability SPELs that are scanner spells]
- `omod_nprk`     : OMOD formid → [perk ids granted via the DATA 'NPRK' tag]

```python
def npc_actor_magfx_keywords(db, rd):
    """Scanner keywords the SPAWNED creature carries as active magic effects
    (func 699). Chain: OBTS -> OMOD(NPRK) -> PERK(type-1 Ability) -> SPEL -> MGEF.KWDA."""
    kws = set()
    for ssig, p in subrecords(rd):
        if ssig == b'OBTS' and len(p) >= 0x12:
            off = 0x12
            while off + 7 <= len(p):
                oid = struct.unpack_from('<I', p, off)[0]; off += 7
                for perk in db.omod_nprk.get(oid, ()):
                    for sp in db.perk_ability_spel.get(perk, ()):
                        kws |= db.spel_scan_kw.get(sp, set())
    return kws

def npc_ability_markers(db, npc_rd):
    """Subset of {0x002634BF, 0x002634C1, 0x002634C0} the NPC_ qualifies for."""
    kws = npc_actor_magfx_keywords(db, npc_rd)
    return {SCANNER_KW_TO_MARKER[k] for k in kws}
```

Func-699 eval wiring (in `eval_leaf`, with `Ctx.actor_magfx`):
```python
if f == MAGFX_FUNC:                       # 699 HasMagicEffectKeyword
    if ctx.actor_magfx is None:           # species path: no live actor -> drop marker
        return None
    return 1.0 if item['p1'] in ctx.actor_magfx else 0.0
```
`PERK_FUNCS` is now `{448}` only (func 448 HasPerk stays dropped: it tests the
PLAYER's `Skill_Zoology` perk for the NonLethalHarvest/Domesticate *display* markers
— not the creature). `derive_fauna(..., actor_scan=False)` keeps `actor_magfx=None`
(species 17/17); `actor_scan=True` populates it from `npc_actor_magfx_keywords`.

---

## VALIDATION RESULTS

`python re/tools/esm_derive_markers.py --validate`  → **17/17 EXACT MATCH** (unchanged).

`python re/tools/esm_derive_markers.py --actor-scan-report`:

- **9 Jemison GT fauna — actor path adds ONLY func-699 markers** (asserted
  `delta ⊆ {Abilities,Resistances,Weaknesses}` and `delta == npc_ability_markers`):
  - Prey03 `0x0019B89A` (reefwalker, mod_CCT_Attack_Poison): **+Abilities**
  - Prey02 `0x0019B89B`: +Abilities,Weaknesses
  - Predator03 `0x0019B89D`: +Abilities,Resistances
  - Predator02 `0x0019B89E`, Predator01 `0x0019B89F`: +Abilities
  - Predator04, Critter01/02, Prey01: (none)
  The species slot+0x08 set for every one of these is **byte-identical** to before.

- **Venomous creature:** Prey03 `0x0019B89A` (reefwalker-skinned + poison attack) →
  ability markers `[0x002634BF]`. Specific MGEF: `0x00169220 CCT_Scan_Attack_Poison`
  (via SPEL `0x00169210 CCT_Scan_Attack_Poison_P`, perk `0x00255D90
  CCT_HitSpell_Poison_Perk`, OMOD `0x00256152 mod_CCT_Attack_Poison`). This is the
  "Abilities: Venomous" attribute on the live actor. **PASS.**

- **Prevalence (NPC_ with OBTS = 1276):** 542 gain ≥1 actor marker.
  - Abilities `0x002634BF`: 410
  - Resistances `0x002634C1`: 262
  - Weaknesses `0x002634C0`: 36

  (A looser raw u32 scan over OMOD DATA counted 465 Abilities / 576 any; the rigorous
  type-1-only count is lower because it excludes perks that reference a scanner spell
  only in a type-2 hit entry. The rigorous count is correct: those do not put the MGEF
  on the creature.)

## LIMITATIONS / HONESTY
- The "exactly 4 markers in-game" ground truth refers to the **species slot+0x08**
  surface; it does NOT mean these creatures show no Abilities attribute when scanned
  live. The structural ESM evidence (and this derivation) shows several of the 9 DO
  carry ability MGEFs — those appear as extra attributes on the spawned actor, which
  is a different surface from the green species slot. No in-game live-scan capture of
  these specific creatures' 5th attribute was available to cross-check pixel-for-pixel;
  the derivation is grounded in the decoded record chain, which is identical in form
  to the already-validated temperament (NKEY) chain.
- Creatures whose abilities arrive ONLY via an unresolved TPLT/leveled template (not
  via their own OBTS) would not resolve offline. None were observed among the
  surveyable base PCM_* fauna — they carry their attack/combat OMODs directly.
