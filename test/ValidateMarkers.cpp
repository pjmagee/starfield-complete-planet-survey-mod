// Offline validation harness for the EsmReader marker derivation.
//
// Compiles the REAL src/EsmReader.cpp (included below) and runs Esm::GetSpeciesMarkers for the 17
// in-game ground-truth species on Jemison (planet 0x0003F5A1), comparing against the known-correct
// slot+0x08 sets — the same 17/17 that re/tools/esm_derive_markers.py validates. This proves the C++
// port is equivalent to the validated Python BEFORE any in-game test.
//
// Build (see test/build_validate.bat): cl /std:c++23 /EHsc /I include /I <zlib> /I <spdlog> ...
// Run: set CPS_ESM_PATH=<path to Starfield.esm>  &&  ValidateMarkers.exe

#include "../src/EsmReader.cpp"  // pulls in the real derivation + Esm::GetSpeciesMarkers

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

namespace
{
    constexpr std::uint32_t kPlanet = 0x0003F5A1;  // Jemison

    struct Case
    {
        std::uint32_t              species;
        std::set<std::uint32_t>    expected;
    };

    // Ground truth — identical to GT in esm_derive_markers.py.
    const std::vector<Case> kCases = {
        // FLORA (FLOR)
        {0x00185478, {0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171867}},
        {0x00185479, {0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171869, 0x00171867}},
        {0x0018547F, {0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171867}},
        {0x00185489, {0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171867}},
        {0x001854C1, {0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171867}},
        {0x001854D8, {0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171867}},
        {0x002F80A0, {0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171869, 0x00171867}},
        {0x002F80BB, {0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171867}},
        // FAUNA (NPC_)
        {0x00048A34, {0x00280178, 0x0023E90D, 0x002634BE, 0x002634C2}},
        {0x0019B898, {0x002634AE, 0x0023E90D, 0x002634BE, 0x002634C2}},
        {0x0019B899, {0x002634AD, 0x0023E90D, 0x002634BE, 0x002634C2}},
        {0x0019B89A, {0x00280178, 0x0023E90D, 0x002634BE, 0x002634C2}},
        {0x0019B89B, {0x002634AD, 0x0023E90D, 0x002634BE, 0x002634C2}},
        {0x0019B89C, {0x00280178, 0x0023E90D, 0x002634BE, 0x002634C2}},
        {0x0019B89D, {0x001699B2, 0x0023E90D, 0x002634BE, 0x002634C2}},
        {0x0019B89E, {0x00280178, 0x0023E90D, 0x002634BE, 0x002634C2}},
        {0x0019B89F, {0x00280172, 0x0023E90D, 0x002634BE, 0x002634C2}},
    };

    std::string HexSet(const std::set<std::uint32_t>& s)
    {
        std::string out = "[";
        for (auto it = s.begin(); it != s.end(); ++it)
        {
            char b[16];
            std::snprintf(b, sizeof b, "%s0x%08X", it == s.begin() ? "" : " ", *it);
            out += b;
        }
        out += "]";
        return out;
    }

    // --- Synthetic-override plugin (PR #26 review: last-non-empty-GNAM-wins) --------------------
    // Writes a minimal second master to %TEMP%: TES4(MAST=Starfield.esm) + one PNDT override of
    // Kreet (0x0003F59F) whose body carries ONLY an EDID — no PPBD, no KWDA and crucially NO
    // 12-byte GNAM. This models a DLC/Creation override that only patches non-galaxy data. The
    // parse must (a) APPLY the override (wholesale-replace semantics erase Kreet's species/traits)
    // while (b) RETAINING the base file's star id (88327) — erasing it would orphan the planet from
    // CompleteSystem. Returns the file path, or empty on write failure (case is then skipped).
    constexpr std::uint32_t kKreet       = 0x0003F59F;
    constexpr std::uint32_t kKreetStarId = 88327;  // NarionStar (STDT DNAM)

    void AppendU16(std::vector<std::uint8_t>& v, std::uint16_t x)
    {
        v.push_back(static_cast<std::uint8_t>(x & 0xFF));
        v.push_back(static_cast<std::uint8_t>(x >> 8));
    }
    void AppendU32(std::vector<std::uint8_t>& v, std::uint32_t x)
    {
        for (int i = 0; i < 4; ++i)
            v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF));
    }
    void AppendSig(std::vector<std::uint8_t>& v, const char sig[5])
    {
        v.insert(v.end(), sig, sig + 4);
    }
    // Subrecord: sig(4) + size(2) + payload.
    void AppendSub(std::vector<std::uint8_t>& v, const char sig[5], const void* data, std::uint16_t size)
    {
        AppendSig(v, sig);
        AppendU16(v, size);
        const auto* p = static_cast<const std::uint8_t*>(data);
        v.insert(v.end(), p, p + size);
    }
    // Record: sig(4) + dataSize(4) + flags(4) + formid(4) + 8 reserved bytes (revision/version/unk).
    void AppendRecord(std::vector<std::uint8_t>& v, const char sig[5], const std::vector<std::uint8_t>& body,
                      std::uint32_t flags, std::uint32_t formid)
    {
        AppendSig(v, sig);
        AppendU32(v, static_cast<std::uint32_t>(body.size()));
        AppendU32(v, flags);
        AppendU32(v, formid);
        AppendU32(v, 0);
        AppendU16(v, 0);
        AppendU16(v, 0);
        v.insert(v.end(), body.begin(), body.end());
    }

    std::filesystem::path WriteSyntheticKreetOverride()
    {
        std::vector<std::uint8_t> file;

        // TES4 header record: just the MAST back-reference (ReadTes4Header only consumes MAST).
        std::vector<std::uint8_t> tes4;
        static const char         kMaster[] = "Starfield.esm";
        AppendSub(tes4, "MAST", kMaster, sizeof kMaster);  // includes the NUL
        AppendRecord(file, "TES4", tes4, /*flags: ESM*/ 0x1, 0);

        // The PNDT override record: EDID only — no PPBD/KWDA/GNAM.
        std::vector<std::uint8_t> pndt;
        static const char         kEdid[] = "KreetOverrideNoGnam";
        AppendSub(pndt, "EDID", kEdid, sizeof kEdid);
        std::vector<std::uint8_t> rec;
        AppendRecord(rec, "PNDT", pndt, /*uncompressed*/ 0, kKreet);

        // Top-level PNDT type-group: GRUP hdr = sig(4) + gsize(4, INCLUDES the 24-byte header) +
        // label(4) + gtype(4) + 8 misc bytes.
        AppendSig(file, "GRUP");
        AppendU32(file, static_cast<std::uint32_t>(24 + rec.size()));
        AppendSig(file, "PNDT");
        AppendU32(file, 0);  // gtype 0 = top-level type group
        AppendU32(file, 0);
        AppendU32(file, 0);
        file.insert(file.end(), rec.begin(), rec.end());

        std::error_code ec;
        const auto      dir = std::filesystem::temp_directory_path(ec);
        if (ec)
            return {};
        const auto    path = dir / "cps_validate_kreet_override.esm";
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size())))
            return {};
        return path;
    }
}

int main(int argc, char** argv)
{
    // Ad-hoc mode: ValidateMarkers.exe <speciesHex> <planetHex> -> print the C++ derivation (mirrors
    // `python esm_derive_markers.py <species> <planet>` for spot-checking non-Jemison / non-default cases).
    if (argc >= 3)
    {
        const auto species = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 16));
        const auto planet  = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 16));
        const auto v       = Esm::GetSpeciesMarkers(species, planet);
        std::set<std::uint32_t> got(v.begin(), v.end());
        std::printf("0x%08X on planet 0x%08X -> %s\n", species, planet, HexSet(got).c_str());
        return 0;
    }

    // --- Load-order setup: base ESM + the synthetic Kreet override (PR #26 review) --------------
    // Must run BEFORE the first Esm:: query (the parse is one-shot). Every check below then runs
    // THROUGH the two-file load order, so the Kreet star-id case doubles as the retention proof.
    bool overrideConfigured = false;
    {
        const char* esm = std::getenv("CPS_ESM_PATH");
        if (esm && *esm && std::filesystem::exists(esm))
        {
            if (const auto ovr = WriteSyntheticKreetOverride(); !ovr.empty())
            {
                Esm::SetSources({{std::filesystem::path {esm}, Esm::MasterType::kFull, 0},
                                 {ovr, Esm::MasterType::kFull, 1}});
                overrideConfigured = true;
                std::printf("[info] synthetic override in load order: %s\n", ovr.string().c_str());
            }
        }
        if (!overrideConfigured)
            std::printf("[warn] synthetic-override case NOT configured (CPS_ESM_PATH missing or temp write failed) — override-retention checks skipped\n");
    }

    int ok = 0;
    std::printf("=== C++ port validation vs ground truth (planet 0x%08X) ===\n", kPlanet);
    for (const auto& c : kCases)
    {
        const auto              v = Esm::GetSpeciesMarkers(c.species, kPlanet);
        std::set<std::uint32_t> got(v.begin(), v.end());
        const bool              pass = (got == c.expected);
        ok += pass;
        if (pass)
            std::printf("  0x%08X OK  %s\n", c.species, HexSet(got).c_str());
        else
        {
            std::printf("  0x%08X MISMATCH\n", c.species);
            std::printf("      derived = %s\n", HexSet(got).c_str());
            std::printf("      expected= %s\n", HexSet(c.expected).c_str());
        }
    }
    std::printf("\n%d/%d EXACT MATCH\n", ok, static_cast<int>(kCases.size()));

    // --- func-699 actor markers (Abilities/Resistances/Weaknesses) — mirrors --actor-scan-report ---
    struct ActorCase
    {
        std::uint32_t           species;
        std::set<std::uint32_t> expected;
    };
    const std::vector<ActorCase> kActorCases = {
        {0x00048A34, {}},                                  // Predator04 — no ability
        {0x0019B898, {}},                                  // Critter02  — no ability
        {0x0019B89A, {0x002634BF}},                        // Prey03 (venomous) — Abilities
        {0x0019B89B, {0x002634BF, 0x002634C0}},            // Prey02 — Abilities + Weaknesses
        {0x0019B89D, {0x002634BF, 0x002634C1}},            // Predator03 — Abilities + Resistances
        {0x0019B89E, {0x002634BF}},                        // Predator02 — Abilities
        {0x0019B89F, {0x002634BF}},                        // Predator01 — Abilities
    };
    int aok = 0;
    std::printf("\n=== func-699 actor markers ===\n");
    for (const auto& c : kActorCases)
    {
        const auto              v = Esm::GetSpeciesActorMarkers(c.species);
        std::set<std::uint32_t> got(v.begin(), v.end());
        const bool              pass = (got == c.expected);
        aok += pass;
        std::printf("  0x%08X %-8s %s\n", c.species, pass ? "OK" : "MISMATCH", HexSet(got).c_str());
    }
    std::printf("%d/%d actor-marker cases OK\n", aok, static_cast<int>(kActorCases.size()));

    // --- Planet -> parent-star ids (PNDT GNAM[0] == parent STDT DNAM; issue #16 system scope) ---
    // Ground truth read straight from Starfield.esm's STDT records: Jemison -> AlphaCentauriStar,
    // Akila -> CheyenneStar, Kreet -> NarionStar, Earth -> SolStar. Sol's star id is 0 — a VALID
    // id — so the Earth case also proves the presence-not-nonzero membership semantics.
    struct StarCase
    {
        const char*   name;
        std::uint32_t planet;
        std::uint32_t starId;
    };
    const std::vector<StarCase> kStarCases = {
        {"Jemison", 0x0003F5A1, 71456},        // AlphaCentauriStar
        {"Akila",   0x0005E2B6, 72432},        // CheyenneStar
        {"Kreet",   kKreet,     kKreetStarId}, // NarionStar — read THROUGH the GNAM-less override (retention)
        {"Earth",   0x0005DEB5, 0},            // SolStar (star id 0 is VALID)
    };
    int         sok   = 0;
    const auto& stars = Esm::GetPlanetStarIds();
    std::printf("\n=== planet -> parent-star ids (%zu planets mapped) ===\n", stars.size());
    for (const auto& c : kStarCases)
    {
        const auto it   = stars.find(c.planet);
        const bool pass = (it != stars.end() && it->second == c.starId);
        sok += pass;
        if (it == stars.end())
            std::printf("  %-8s 0x%08X MISSING (expected starId=%u)\n", c.name, c.planet, c.starId);
        else
            std::printf("  %-8s 0x%08X %-8s starId=%u (expected %u)\n", c.name, c.planet,
                        pass ? "OK" : "MISMATCH", it->second, c.starId);
    }
    // Coverage floor: the base game authors GNAM on every PNDT (1765 in Starfield.esm alone) —
    // a mostly-empty map means the subrecord parse regressed even if the four cases pass.
    const bool starCoverage = stars.size() >= 1700;
    if (!starCoverage)
        std::printf("  COVERAGE FAIL: only %zu planets carry a star id (expected >= 1700)\n", stars.size());
    std::printf("%d/%d star-id cases OK, coverage %s\n", sok, static_cast<int>(kStarCases.size()),
                starCoverage ? "OK" : "FAIL");

    // --- Override-retention proof (PR #26 review) ---
    // The Kreet star case above already ran THROUGH the GNAM-less override, so retention is
    // asserted there; this section proves the override actually APPLIED — Kreet hosts authored
    // fauna in the base game, so its wholesale-replaced (empty-PPBD) override must have ERASED it
    // from the species map. Without this, a malformed synthetic file that failed to parse would
    // make the retention case pass vacuously.
    bool overridePass = true;
    if (overrideConfigured)
    {
        const bool applied  = Esm::GetPlanetSpecies().count(kKreet) == 0;
        const auto kit      = stars.find(kKreet);
        const bool retained = kit != stars.end() && kit->second == kKreetStarId;
        overridePass        = applied && retained;
        std::printf("\n=== synthetic override (Kreet PNDT override WITHOUT a 12-byte GNAM) ===\n");
        std::printf("  override applied  (base species erased by the override): %s\n", applied ? "OK" : "FAIL");
        std::printf("  star id retained  (88327 survives the GNAM-less override): %s\n", retained ? "OK" : "FAIL");
    }

    const bool allPass = ok == static_cast<int>(kCases.size()) && aok == static_cast<int>(kActorCases.size()) &&
                         sok == static_cast<int>(kStarCases.size()) && starCoverage && overridePass;
    return allPass ? 0 : 1;
}
