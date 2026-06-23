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

    const bool allPass = ok == static_cast<int>(kCases.size()) && aok == static_cast<int>(kActorCases.size());
    return allPass ? 0 : 1;
}
