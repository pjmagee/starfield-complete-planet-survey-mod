#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

// Offline reader for the authored planet flora/fauna across ALL loaded master files.
//
// A planet's flora/fauna are authored on the PNDT record's PPBD "Per Biome Data"
// subrecords (one per biome). The engine parses them at form-load into a lazy
// CTPerBiomeData component that is empty for un-visited planets — so the only
// ref-free source for an un-visited planet is the plugin file itself. We parse the
// PNDT records' PPBD chunks directly from every configured source file, remapping
// each file's local FormIDs to runtime FormIDs (full/medium/small master index
// spaces), with later files overriding earlier ones — so DLC/Creations planets
// (ShatteredSpace.esm, SFBGS00D.esm, SFBGS050.esm, …) and DLC overrides of base
// planets resolve exactly like they do in the running game.
namespace Esm
{
    // How a plugin occupies the runtime FormID space.
    //   kFull:   index = FormID byte 3            (II XXXXXX)
    //   kMedium: FormID = FD II XXXX (8-bit index, 16-bit record id)
    //   kSmall:  FormID = FE IIIXXX  (12-bit index, 12-bit record id)
    enum class MasterType : std::uint8_t
    {
        kFull = 0,
        kMedium,
        kSmall,
    };

    // One loaded plugin: where it is on disk and where the ENGINE put it in the
    // runtime FormID space. `runtimeIndex` is the compile index within the file's
    // own type space (full byte / medium 8-bit / small 12-bit slot).
    struct SourceFile
    {
        std::filesystem::path path;
        MasterType            type {MasterType::kFull};
        std::uint16_t         runtimeIndex {0};
    };

    // Configure the files to parse, in LOAD ORDER (later files override earlier
    // ones). Call before the first GetPlanetSpecies/GetSpeciesMarkers query (the
    // parse is one-shot and cached). If never called, falls back to:
    //   1. CPS_ESM_PATHS  — ';'-separated file list treated as the whole load
    //      order; runtime indices assigned per type in list order (offline tests).
    //   2. CPS_ESM_PATH   — a single Starfield.esm (legacy offline harness).
    //   3. <exe>\Data\Starfield.esm.
    void SetSources(std::vector<SourceFile> sources);

    // planetFormID -> deduped list of flora + fauna species FormIDs for that planet.
    // All FormIDs (keys and values) are RUNTIME FormIDs.
    using PlanetSpeciesMap = std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>;

    // Parse the source files once (cached, thread-safe). Returns an empty map if
    // nothing could be read. Cheap on subsequent calls.
    const PlanetSpeciesMap& GetPlanetSpecies();

    // The full slot+0x08 "green marker" set for one species (FLOR or NPC_), derived purely from
    // the plugin files — no game, no visiting, no live instance. This is the array the engine copies
    // into a scanned species' slot+0x08; writing it under the render key greens the species remotely.
    //
    // Derivation evaluates the AUTHORED CTDA conditions on each marker in the HandScanner catalog FLST
    // (flora 0x00160C96 / fauna 0x00160C97, including any DLC override of those lists), with leaf
    // functions resolved offline: 858 GetIsPlanetTrait -> `planetFormId`'s PNDT KWDA; 560 HasKeyword ->
    // the NPC_'s OBTS->OMOD->NKEY granted set; 14 GetActorValue -> the FLOR PRPS reproduction value;
    // 837 -> recurse into the marker's CNDF (genetics + reproduction sub-trees); 448/699 perk/display
    // funcs -> marker dropped. This reproduces the engine's slot+0x08 set EXACTLY (validated 17/17 vs
    // in-game ground truth in esm_derive_markers.py), so every green-set field (resource / biomes /
    // genetics / reproduction / temperament / health) comes out per-species and per-planet correct —
    // no hardcoded fallbacks.
    //
    // Returns an empty vector if the form is unknown / unparseable (caller leaves the species blue).
    // `planetFormId` gates flora genetics/reproduction (func 858); pass 0 for the trait-free default.
    // Both ids are RUNTIME FormIDs.
    std::vector<std::uint32_t> GetSpeciesMarkers(std::uint32_t speciesFormId,
                                                 std::uint32_t planetFormId = 0);

    // The func-699 actor-scan markers for a FAUNA species — Abilities (0x002634BF), Resistances
    // (0x002634C1), Weaknesses (0x002634C0) — derived from the creature's static ability attachments
    // (NPC_ OBTS -> OMOD NPRK -> PERK -> SPEL -> MGEF scanner keyword). These are the extra attributes
    // the LIVE HandScanner shows beyond the per-species slot+0x08 set; the mod writes them too so a
    // remotely-greened creature's panel matches a real scan. Empty for flora / no-ability fauna.
    std::vector<std::uint32_t> GetSpeciesActorMarkers(std::uint32_t speciesFormId);
}
