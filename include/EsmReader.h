#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

// Runtime reader for the authored planet flora/fauna in Starfield.esm.
//
// A planet's flora/fauna are authored on the PNDT record's PPBD "Per Biome Data"
// subrecords (one per biome). The engine parses them at form-load into a lazy
// CTPerBiomeData component that is empty for un-visited planets — so the only
// ref-free source for an un-visited planet is the ESM file itself. We parse the
// PNDT records' PPBD chunks directly. Starfield.esm is always load-order 00, so
// the FormIDs in the file equal the runtime FormIDs.
namespace Esm
{
    // planetFormID -> deduped list of flora + fauna species FormIDs for that planet.
    using PlanetSpeciesMap = std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>;

    // (planetFormID<<32 | speciesFormID) -> number of biomes that species is authored in on the
    // planet (one PPBD subrecord per biome). Multi-biome flora (>=3 biomes) carry an extra slot+0x08
    // marker (0x00171869), so the green build needs this count to write the correct per-species set.
    using SpeciesBiomeCount = std::unordered_map<std::uint64_t, std::uint8_t>;

    // Parse Starfield.esm once (cached, thread-safe). Returns an empty map if the
    // file can't be found/read. Cheap on subsequent calls.
    const PlanetSpeciesMap& GetPlanetSpecies();

    // Biome count for one (planet, species); 0 if unknown. Triggers the same one-time ESM parse.
    std::uint8_t GetSpeciesBiomeCount(std::uint32_t planetId, std::uint32_t speciesId);

    // The full slot+0x08 "green marker" set for one species (FLOR or NPC_), derived purely from
    // Starfield.esm — no game, no visiting, no live instance. This is the array the engine copies
    // into a scanned species' slot+0x08; writing it under the render key greens the species remotely.
    //
    // Derivation evaluates the AUTHORED CTDA conditions on each marker in the HandScanner catalog FLST
    // (flora 0x00160C96 / fauna 0x00160C97), with leaf functions resolved offline: 858 GetIsPlanetTrait
    // -> `planetFormId`'s PNDT KWDA; 560 HasKeyword -> the NPC_'s OBTS->OMOD->NKEY granted set; 14
    // GetActorValue -> the FLOR PRPS reproduction value; 837 -> recurse into the marker's CNDF (genetics
    // + reproduction sub-trees); 448/699 perk/display funcs -> marker dropped. This reproduces the
    // engine's slot+0x08 set EXACTLY (validated 17/17 vs in-game ground truth in esm_derive_markers.py),
    // so every green-set field (resource / biomes / genetics / reproduction / temperament / health)
    // comes out per-species and per-planet correct — no hardcoded fallbacks.
    //
    // Returns an empty vector if the form is unknown / unparseable (caller leaves the species blue).
    // `planetFormId` gates flora genetics/reproduction (func 858); pass 0 for the trait-free default.
    std::vector<std::uint32_t> GetSpeciesMarkers(std::uint32_t speciesFormId,
                                                 std::uint32_t planetFormId = 0);

    // The func-699 actor-scan markers for a FAUNA species — Abilities (0x002634BF), Resistances
    // (0x002634C1), Weaknesses (0x002634C0) — derived from the creature's static ability attachments
    // (NPC_ OBTS -> OMOD NPRK -> PERK -> SPEL -> MGEF scanner keyword). These are the extra attributes
    // the LIVE HandScanner shows beyond the per-species slot+0x08 set; the mod writes them too so a
    // remotely-greened creature's panel matches a real scan. Empty for flora / no-ability fauna.
    std::vector<std::uint32_t> GetSpeciesActorMarkers(std::uint32_t speciesFormId);
}
