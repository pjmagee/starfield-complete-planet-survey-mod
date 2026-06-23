#!/usr/bin/env python3
"""
esm_derive_markers.py  --  UNIFIED offline slot+0x08 marker-set deriver.

Given a species form-id (FLOR or NPC_) and an optional planet form-id (PNDT),
parse Starfield.esm and compute the EXACT slot+0x08 marker set the engine's
DumpSpeciesSlots produces -- by EVALUATING the real authored conditions:
  * catalog FLST 0x00160C96 (flora) / 0x00160C97 (fauna)   -- inline CTDA per marker
  * CNDF recursion (func 837)                              -- genetics + reproduction trees
  * func 858 GetIsPlanetTrait  -> planet PNDT KWDA membership
  * func 14  GetActorValue(0x0023E905) -> FLOR PRPS reproduction value N
  * func 560 HasKeyword(species) -> the species' GRANTED keyword set, where
             fauna temperament/underground keywords are granted via
             OBTS -> OMOD(mod_CCT_*) -> DATA 'NKEY' tag.
  * func 448 HasPerk / func 699 HasMagicEffectKeyword -> treated as the
             display/perk-gated markers that NEVER enter the green slot+0x08
             set (excluded -- proven by ground truth).

There are NO hard-coded per-species marker tables. The only hard-coded data are
the catalog form-ids (0x160C96/0x160C97), the reproduction AVIF (0x0023E905),
and the perk/magic-effect leaf-function ids -- all of which are catalog-level
constants, not per-species answers.

A minimal, faithful CTDA evaluator (TESCondition::IsTrue == engine ID_71422 +
per-item ID_71429) implements: comparison op = (op>>5)&7 {==,!=,>,>=,<,<=};
OR-with-next = op&1 (consecutive OR-flagged items form an OR-run, runs AND
together); func 882 = constant 1.0 anchor.

Usage:
  python esm_derive_markers.py <speciesHex> [planetHex]
  python esm_derive_markers.py --validate          # run the 17 ground-truth species
"""
import struct, zlib, sys

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

CATALOG_FLORA = 0x00160C96
CATALOG_FAUNA = 0x00160C97
REPRO_AVIF    = 0x0023E905

# func 448 = HasPerk (gated on the PLAYER's perk, e.g. Skill_Zoology) -> never
# resolvable from a species and never part of any species/actor set we derive.
PERK_FUNCS = {448}

# func 699 = HasMagicEffectKeyword. This is an ACTOR-scan condition: it asks whether
# a spawned creature carries an active magic effect whose MGEF holds keyword param1.
# It is NOT part of the per-species slot+0x08 green set (a bare species has no live
# actor, so the engine's DumpSpeciesSlots evaluates it false -> the 17/17 ground
# truth stays 4 markers). It IS resolvable offline from the species' static ability
# attachments (OBTS->OMOD->NPRK->PERK type-1 Ability entry->SPEL->EFID->MGEF->KWDA),
# which is what the live in-game HandScanner shows as the extra Abilities/Resistances/
# Weaknesses attributes. We resolve it only on the explicit actor-scan path.
MAGFX_FUNC = 699

# The three func-699 scanner keywords and the catalog markers they gate.
SCANNER_KW_TO_MARKER = {
    0x001D3B47: 0x002634BF,   # HandScannerActorAbilityEffectKeyword    -> HandScannerActorAbilities
    0x001D3B48: 0x002634C1,   # HandScannerActorResistanceEffectKeyword -> HandScannerActorResistances
    0x001D3B46: 0x002634C0,   # HandScannerActorWeaknessEffectKeyword   -> HandScannerActorWeaknesses
}
SCANNER_KEYWORDS = set(SCANNER_KW_TO_MARKER)

# ---------- low-level ESM parsing ----------
def subrecords(rd):
    j=0; real=None
    while j+6<=len(rd):
        ssig=rd[j:j+4]; ssz=struct.unpack_from('<H',rd,j+4)[0]
        if ssig==b'XXXX': real=struct.unpack_from('<I',rd,j+6)[0]; j+=6+ssz; continue
        dsz=ssz
        if real is not None: dsz=real; real=None
        yield ssig, rd[j+6:j+6+dsz]; j+=6+dsz

def inflate(rd,flags):
    if flags&COMPRESSED and len(rd)>=4:
        try: return zlib.decompress(rd[4:])
        except: return b''
    return rd

def topwalk(data, t, labels):
    pos=24+t; n=len(data); out=[]
    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            out.append((sig, ff, inflate(d[i+24:i+24+size], flags)))
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; lab=data[pos+8:pos+12]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0 and lab in labels: walk(data[pos+24:pos+gs])
        pos+=gs
    return out

def edid_of(rd):
    for ssig,p in subrecords(rd):
        if ssig==b'EDID': return p.rstrip(b'\x00').decode('latin1','replace')
    return None

# ---------- the ESM database (loaded once) ----------
class EsmDB:
    def __init__(self, path=ESM):
        with open(path,'rb') as f: self.data=f.read()
        self.t=struct.unpack_from('<I',self.data,4)[0]
        self.flor={}; self.npc={}; self.pndt={}; self.flst={}; self.cndf={}
        self.omod_nkey={}            # omodid -> [nkey kw ids]
        self.omod_nprk={}            # omodid -> [perk ids granted via NPRK]
        self.kywd_edid={}
        self.mgef_scan_kw={}         # mgefid -> set(scanner kw ids in its KWDA)
        self.spel_scan_kw={}         # spelid -> set(scanner kw ids via its EFID->MGEF)
        self.perk_ability_spel={}    # perkid -> [spel ids added as type-1 Ability]
        # FLST (catalogs) + CNDF + KYWD: small enough to grab all
        for sig,ff,rd in topwalk(self.data,self.t,{b'FLST',b'CNDF',b'KYWD'}):
            if sig==b'FLST': self.flst[ff]=rd
            elif sig==b'CNDF': self.cndf[ff]=rd
            elif sig==b'KYWD': self.kywd_edid[ff]=edid_of(rd)
        # MGEF: which carry a scanner keyword in KWDA
        for sig,ff,rd in topwalk(self.data,self.t,{b'MGEF'}):
            if sig!=b'MGEF': continue
            for ssig,p in subrecords(rd):
                if ssig==b'KWDA':
                    kws=set(struct.unpack_from('<%dI'%(len(p)//4),p,0)) & SCANNER_KEYWORDS
                    if kws: self.mgef_scan_kw[ff]=kws
        # SPEL: EFID list -> scanner kws via its MGEFs
        for sig,ff,rd in topwalk(self.data,self.t,{b'SPEL'}):
            if sig!=b'SPEL': continue
            kws=set()
            for ssig,p in subrecords(rd):
                if ssig==b'EFID' and len(p)>=4:
                    mid=struct.unpack_from('<I',p,0)[0]
                    kws|=self.mgef_scan_kw.get(mid, set())
            if kws: self.spel_scan_kw[ff]=kws
        # PERK: type-1 (Ability) entries add a SPEL to the perk owner (PRKE type byte
        # == 1, the following DATA's first u32 is the ability SPEL formid). Only these
        # make the creature itself HOLD the ability MGEF -> HasMagicEffectKeyword true.
        # (Type-2 EntryPoint / hit-spell entries apply to the TARGET, not self.)
        for sig,ff,rd in topwalk(self.data,self.t,{b'PERK'}):
            if sig!=b'PERK': continue
            spells=[]; cur=None
            for ssig,p in subrecords(rd):
                if ssig==b'PRKE':
                    cur = p[0] if len(p)>=1 else None
                elif ssig==b'DATA' and cur==1 and len(p)>=4:
                    sp=struct.unpack_from('<I',p,0)[0]
                    if sp in self.spel_scan_kw: spells.append(sp)
                    cur=None     # consume the entry's DATA
            if spells: self.perk_ability_spel[ff]=spells
        # OMOD: build NKEY map (temperament/enviro keyword grants) AND NPRK map
        # (perk grants -- abilities/resistances/weaknesses live here, same property
        # table as NKEY, just a different 4-ascii tag).
        for sig,ff,rd in topwalk(self.data,self.t,{b'OMOD'}):
            if sig!=b'OMOD': continue
            nk=[]; pk=[]
            for ssig,p in subrecords(rd):
                if ssig==b'DATA':
                    o=0
                    while o+8<=len(p):
                        tag=p[o:o+4]
                        if tag==b'NKEY': nk.append(struct.unpack_from('<I',p,o+4)[0]); o+=8; continue
                        if tag==b'NPRK': pk.append(struct.unpack_from('<I',p,o+4)[0]); o+=8; continue
                        o+=1
            if nk: self.omod_nkey[ff]=nk
            if pk: self.omod_nprk[ff]=pk
        # FLOR / NPC_ / PNDT: index by form-id
        for sig,ff,rd in topwalk(self.data,self.t,{b'FLOR',b'NPC_',b'PNDT'}):
            if sig==b'FLOR': self.flor[ff]=rd
            elif sig==b'NPC_': self.npc[ff]=rd
            elif sig==b'PNDT': self.pndt[ff]=rd

    def kwn(self, fid): return self.kywd_edid.get(fid, hex(fid))

# ---------- per-record extractors ----------
def pndt_traits(rd):
    """planet trait keyword set from PNDT KWDA."""
    for ssig,p in subrecords(rd):
        if ssig==b'KWDA':
            return set(struct.unpack_from('<%dI'%(len(p)//4),p,0))
    return set()

def flor_prps_n(rd):
    for ssig,p in subrecords(rd):
        if ssig==b'PRPS':
            o=0
            while o+12<=len(p):
                avif,val,extra=struct.unpack_from('<IfI',p,o)
                if avif==REPRO_AVIF: return round(val)
                o+=12
    return None

def npc_granted_keywords(db, rd):
    """The keyword set the engine GRANTS this NPC_ at build time, via
    OBTS -> OMOD(mod_CCT_*) -> DATA 'NKEY'. This is exactly what func-560
    HasKeyword(species) tests against in the fauna catalog."""
    granted=set()
    for ssig,p in subrecords(rd):
        if ssig==b'OBTS' and len(p)>=0x12:
            off=0x12
            while off+7<=len(p):
                oid=struct.unpack_from('<I',p,off)[0]; off+=7
                for k in db.omod_nkey.get(oid, ()): granted.add(k)
    return granted

def npc_actor_magfx_keywords(db, rd):
    """The set of func-699 SCANNER keywords the SPAWNED creature will carry as
    active magic effects -- i.e. what HasMagicEffectKeyword(K) returns true for.

    Chain (each link decoded + cited in the report):
      NPC_ OBTS  -> 7-byte entries from off 0x12, u32 OMOD formid each
      OMOD DATA  -> property table; 'NPRK' tag (type-2) + u32 PERK formid grants a perk
      PERK PRKE  -> entry header; type byte == 1 means an Ability entry whose DATA's
                    first u32 is the SPEL added to the OWNER (the creature itself)
      SPEL EFID  -> u32 MGEF formid per effect
      MGEF KWDA  -> contains one of the 3 HandScanner*EffectKeyword scanner keywords

    Type-2 EntryPoint / hit-spell perk entries are intentionally excluded: they apply
    a spell to the creature's TARGET on hit, so the creature itself does not hold the
    MGEF and HasMagicEffectKeyword would be false for it. (In practice the CCT perks
    pair a type-1 self-Ability entry with the type-2 hit entry, so the self-ability is
    what the scanner reads -- confirmed: every scanner perk has a type-1 entry.)"""
    kws=set()
    for ssig,p in subrecords(rd):
        if ssig==b'OBTS' and len(p)>=0x12:
            off=0x12
            while off+7<=len(p):
                oid=struct.unpack_from('<I',p,off)[0]; off+=7
                for perk in db.omod_nprk.get(oid, ()):
                    for sp in db.perk_ability_spel.get(perk, ()):
                        kws |= db.spel_scan_kw.get(sp, set())
    return kws

def npc_ability_markers(db, npc_rd):
    """Return the subset of {0x002634BF, 0x002634C1, 0x002634C0} (Abilities/
    Resistances/Weaknesses) that this NPC_ qualifies for, per func-699
    HasMagicEffectKeyword evaluated against the creature's static ability set."""
    kws = npc_actor_magfx_keywords(db, npc_rd)
    return {SCANNER_KW_TO_MARKER[k] for k in kws}

# ---------- CTDA structures ----------
def parse_ctda(p):
    op=p[0]; comp=struct.unpack_from('<f',p,4)[0]; func=struct.unpack_from('<H',p,8)[0]
    p1=struct.unpack_from('<I',p,0xC)[0]
    return {'op':op,'comp':comp,'func':func,'p1':p1,
            'orwith':op&1, 'cmp':(op>>5)&7}

def parse_flst(rd):
    """return (lnam[], blocks=[(marker_idx, [ctda...])])."""
    lnam=[]; blocks=[]; curidx=-1; cur=[]
    started=False
    for ssig,p in subrecords(rd):
        if ssig==b'LNAM' and len(p)>=4:
            lnam.append(struct.unpack_from('<I',p,0)[0])
        elif ssig==b'INAM' and len(p)>=4:
            if started: blocks.append((curidx,cur))
            curidx=struct.unpack_from('<I',p,0)[0]; cur=[]; started=True
        elif ssig==b'CTDA' and len(p)>=0x20:
            cur.append(parse_ctda(p))
    if started: blocks.append((curidx,cur))
    return lnam, blocks

def cndf_ctdas(rd):
    out=[]
    for ssig,p in subrecords(rd):
        if ssig==b'CTDA' and len(p)>=0x20: out.append(parse_ctda(p))
    return out

# ---------- the CTDA evaluator (engine ID_71422 / ID_71429) ----------
CMP = {
    0: lambda a,b: a==b,
    1: lambda a,b: a!=b,
    2: lambda a,b: a>b,
    3: lambda a,b: a>=b,
    4: lambda a,b: a<b,
    5: lambda a,b: a<=b,
}

class Ctx:
    """Evaluation context for one species on one planet."""
    def __init__(self, db, species_keywords, planet_traits, repro_av, actor_magfx=None):
        self.db=db
        self.skw=species_keywords     # set of keyword ids the species HAS (func 560)
        self.traits=planet_traits     # set of planet trait kw ids (func 858)
        self.av=repro_av              # GetActorValue(0x0023E905) (func 14); may be None
        # actor_magfx: set of scanner keyword ids the SPAWNED creature carries via
        # active magic effects (func 699 HasMagicEffectKeyword). None on the per-
        # species slot+0x08 path (no live actor -> func 699 returns None -> the
        # gated marker is dropped, preserving the 17/17 species ground truth).
        self.actor_magfx=actor_magfx

def eval_leaf(item, ctx):
    """Return the engine's float result for a single condition function,
    BEFORE the comparison op is applied. (We return the raw value; the
    comparison happens in eval_item.)"""
    f=item['func']
    if f==882:
        return 1.0                       # constant-1 anchor
    if f==858:
        return 1.0 if item['p1'] in ctx.traits else 0.0   # GetIsPlanetTrait
    if f==560:
        return 1.0 if item['p1'] in ctx.skw else 0.0      # HasKeyword(species)
    if f==14:
        return float(ctx.av) if ctx.av is not None else 0.0  # GetActorValue
    if f==837:
        return 1.0 if eval_condform(item['p1'], ctx) else 0.0  # EvaluateConditionForm
    if f==MAGFX_FUNC:                     # 699 HasMagicEffectKeyword
        # ACTOR-scan condition. On the species path (actor_magfx is None) it cannot
        # be evaluated -> return None so the marker drops (matches engine
        # DumpSpeciesSlots / the 17/17 species ground truth). On the actor-scan path
        # we resolve it from the creature's static ability set.
        if ctx.actor_magfx is None:
            return None
        return 1.0 if item['p1'] in ctx.actor_magfx else 0.0
    if f in PERK_FUNCS:
        # HasPerk (player perk, e.g. Skill_Zoology): cannot resolve offline AND the
        # markers it gates are player-display-only (never in any species/actor set we
        # derive). Return a sentinel so the caller drops the whole marker.
        return None
    # unknown function -> treat as unsatisfiable to be safe (flagged by caller)
    return None

def eval_item(item, ctx):
    """Evaluate one CTDA to a bool. Returns None if the leaf is unresolvable
    (perk/unknown) -- meaning the whole marker is display/unknown gated."""
    v=eval_leaf(item, ctx)
    if v is None: return None
    return CMP[item['cmp']](v, item['comp'])

def eval_condlist(items, ctx):
    """Faithful port of ID_71422: walk items, OR-grouping by op&1, AND the runs.
    Returns bool, or None if any *contributing* leaf is unresolvable."""
    if not items: return True
    acc=True; in_or=False; last=None; unresolved=False
    # We mirror the engine: consecutive op&1 items form an OR-run; an item with
    # op&1==0 closes the current run (or stands alone) and ANDs it into acc.
    run=[]  # current OR-run of bool results
    results=[]  # list of (is_or_with_next, bool-or-None)
    for it in items:
        r=eval_item(it, ctx)
        results.append((it['orwith'], r))
    # group
    groups=[]; cur=[]
    for orwith, r in results:
        cur.append(r)
        if not orwith:
            groups.append(cur); cur=[]
    if cur: groups.append(cur)   # trailing OR with nothing after -> its own group
    for g in groups:
        # OR within group; a None makes that disjunct unknown
        gv=False; gunknown_all=True; any_true=False; any_known=False
        for r in g:
            if r is None: continue
            any_known=True
            if r: any_true=True
        if not any_known:
            # entire group is perk/unknown gated -> marker is display/unknown
            return None
        gv = any_true
        acc = acc and gv
    return acc

def eval_condform(cndf_id, ctx, _stack=None):
    """func 837: EvaluateConditionForm -> recurse into CNDF's CTDA list."""
    rd=ctx.db.cndf.get(cndf_id)
    if rd is None: return False
    items=cndf_ctdas(rd)
    # CNDF recursion can be deep; guard cycles
    r=eval_condlist(items, ctx)
    return bool(r) if r is not None else False

# ---------- top-level derivation ----------
def derive_flora(db, species_id, planet_id):
    rd=db.flor.get(species_id)
    if rd is None: return None, "FLOR not found"
    traits = pndt_traits(db.pndt[planet_id]) if planet_id in db.pndt else set()
    n = flor_prps_n(rd)
    ctx = Ctx(db, species_keywords=set(), planet_traits=traits, repro_av=n)
    lnam, blocks = parse_flst(db.flst[CATALOG_FLORA])
    cond_by_idx={idx:conds for idx,conds in blocks}
    out=set()
    for i,marker in enumerate(lnam):
        if i not in cond_by_idx:
            out.add(marker)         # unconditional (no INAM block) -> always emit
            continue
        r=eval_condlist(cond_by_idx[i], ctx)
        if r is True: out.add(marker)
        # r is None => perk/display gated marker -> excluded (correct)
    return out, None

def derive_fauna(db, species_id, planet_id, actor_scan=False):
    """Derive the fauna marker set.

    actor_scan=False (default): the per-SPECIES slot+0x08 green set. func 699
      (HasMagicEffectKeyword) is unresolvable (no live actor) so its Abilities/
      Resistances/Weaknesses markers drop -> matches engine DumpSpeciesSlots and
      the 17/17 ground truth.
    actor_scan=True: the LIVE in-game HandScanner attribute set, which additionally
      evaluates func 699 against the creature's static ability attachments, adding
      the extra Abilities/Resistances/Weaknesses markers the player sees on a
      spawned creature (e.g. 'Abilities: Venomous')."""
    rd=db.npc.get(species_id)
    if rd is None: return None, "NPC_ not found"
    traits = pndt_traits(db.pndt[planet_id]) if planet_id in db.pndt else set()
    skw = npc_granted_keywords(db, rd)
    magfx = npc_actor_magfx_keywords(db, rd) if actor_scan else None
    ctx = Ctx(db, species_keywords=skw, planet_traits=traits, repro_av=None,
              actor_magfx=magfx)
    lnam, blocks = parse_flst(db.flst[CATALOG_FAUNA])
    cond_by_idx={idx:conds for idx,conds in blocks}
    out=set()
    for i,marker in enumerate(lnam):
        if i not in cond_by_idx:
            out.add(marker)         # unconditional -> always emit (ActorHealth)
            continue
        r=eval_condlist(cond_by_idx[i], ctx)
        if r is True: out.add(marker)
    return out, None

def derive(db, species_id, planet_id=None, actor_scan=False):
    if species_id in db.flor: return derive_flora(db, species_id, planet_id), 'FLOR'
    if species_id in db.npc:  return derive_fauna(db, species_id, planet_id, actor_scan), 'NPC_'
    return (None, "species not FLOR or NPC_"), '?'

# ---------- ground truth ----------
GT_PLANET = 0x0003F5A1
GT = {
 # FLORA (FLOR)
 0x00185478:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
 0x00185479:{0x0023E90D,0x002634BE,0x0023E90C,0x00171869,0x00171867},
 0x0018547F:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
 0x00185489:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
 0x001854C1:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
 0x001854D8:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
 0x002F80A0:{0x0023E90D,0x002634BE,0x0023E90C,0x00171869,0x00171867},
 0x002F80BB:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
 # FAUNA (NPC_)
 0x00048A34:{0x00280178,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B898:{0x002634AE,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B899:{0x002634AD,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89A:{0x00280178,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89B:{0x002634AD,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89C:{0x00280178,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89D:{0x001699B2,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89E:{0x00280178,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89F:{0x00280172,0x0023E90D,0x002634BE,0x002634C2},
}

def hexset(s): return '['+' '.join('0x%08X'%x for x in sorted(s))+']'

def validate(db):
    ok=0; total=len(GT)
    print(f"=== VALIDATION vs ground truth (planet 0x{GT_PLANET:08X}) ===")
    for sid,gt in GT.items():
        (res,err),kind=derive(db, sid, GT_PLANET)
        if res is None:
            print(f"  0x{sid:08X} [{kind}] ERROR {err}"); continue
        match = (res==gt)
        ok+=match
        if match:
            print(f"  0x{sid:08X} [{kind}] OK  {hexset(res)}")
        else:
            print(f"  0x{sid:08X} [{kind}] MISMATCH")
            print(f"      derived={hexset(res)}")
            print(f"      GT     ={hexset(gt)}")
            print(f"      missing={hexset(gt-res)} extra={hexset(res-gt)}")
    print(f"\n{ok}/{total} EXACT MATCH")
    return ok,total

def actor_scan_report(db):
    """Exercise the func-699 actor-scan path: confirm the 9 GT fauna gain only the
    func-699 markers on the actor path (and nothing else), confirm a venomous
    creature gets Abilities, and report prevalence across all surveyable fauna."""
    print("=== ACTOR-SCAN (func 699) DELTA on the 9 Jemison GT fauna ===")
    print("    (species slot+0x08 must be unchanged; actor adds only 0x2634BF/C0/C1)")
    name={0x002634BF:'Abilities',0x002634C1:'Resistances',0x002634C0:'Weaknesses'}
    for sid in [0x00048A34,0x0019B898,0x0019B899,0x0019B89A,0x0019B89B,
                0x0019B89C,0x0019B89D,0x0019B89E,0x0019B89F]:
        (sp,_),_=derive(db, sid, GT_PLANET, actor_scan=False)
        (ac,_),_=derive(db, sid, GT_PLANET, actor_scan=True)
        delta=ac-sp
        am=npc_ability_markers(db, db.npc[sid])
        assert delta==am, f"delta {hexset(delta)} != ability_markers {hexset(am)}"
        assert delta <= {0x002634BF,0x002634C1,0x002634C0}, "actor path added non-699 marker!"
        d='+'+','.join(name[m] for m in sorted(delta)) if delta else '(none)'
        print(f"  0x{sid:08X} {edid_of(db.npc[sid]):42s} actor-delta={d}")

    print("\n=== VENOMOUS CREATURE CHECK ===")
    # Prey03 = reefwalker-skinned, mod_CCT_Attack_Poison -> CCT_Scan_Attack_Poison MGEF
    sid=0x0019B89A
    am=npc_ability_markers(db, db.npc[sid])
    print(f"  0x{sid:08X} {edid_of(db.npc[sid])}")
    print(f"    ability markers = {hexset(am)} -> {[name[m] for m in sorted(am)]}")
    print(f"    {'PASS' if 0x002634BF in am else 'FAIL'}: Abilities (0x002634BF) present")

    print("\n=== PREVALENCE across all NPC_ with OBTS ===")
    cnt={0x002634BF:0,0x002634C1:0,0x002634C0:0}; any_=0; tot=0
    for nid,rd in db.npc.items():
        if not any(s==b'OBTS' for s,_ in subrecords(rd)): continue
        tot+=1
        am=npc_ability_markers(db, rd)
        if am: any_+=1
        for m in am: cnt[m]+=1
    print(f"  NPC_ with OBTS: {tot}; with >=1 actor marker: {any_}")
    for m,nm in name.items():
        print(f"    {nm:12s} (0x{m:08X}): {cnt[m]}")

def main():
    db=EsmDB()
    args=sys.argv[1:]
    if not args or args[0]=='--validate':
        validate(db); return
    if args[0]=='--actor-scan-report':
        actor_scan_report(db); return
    actor = '--actor-scan' in args
    args=[a for a in args if a!='--actor-scan']
    sid=int(args[0],16)
    pid=int(args[1],16) if len(args)>1 else None
    (res,err),kind=derive(db, sid, pid, actor_scan=actor)
    if res is None: print(f"0x{sid:08X} [{kind}] ERROR: {err}"); return
    label = 'actor-scan attrs' if actor else 'slot+0x08'
    print(f"0x{sid:08X} [{kind}] planet={('0x%08X'%pid) if pid else 'none'}")
    print(f"  EDID: {edid_of(db.flor.get(sid) or db.npc.get(sid))}")
    print(f"  {label} = {hexset(res)}")
    for m in sorted(res):
        print(f"     0x{m:08X}  {db.kwn(m)}")

if __name__=='__main__': main()
