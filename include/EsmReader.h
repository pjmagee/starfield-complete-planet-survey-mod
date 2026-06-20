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

    // Parse Starfield.esm once (cached, thread-safe). Returns an empty map if the
    // file can't be found/read. Cheap on subsequent calls.
    const PlanetSpeciesMap& GetPlanetSpecies();
}
