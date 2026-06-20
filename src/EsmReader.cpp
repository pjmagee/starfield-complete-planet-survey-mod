#include "EsmReader.h"

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>  // GetModuleFileNameW, MAX_PATH

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <spdlog/spdlog.h>  // logging — also via the forced PCH, but include-what-you-use

namespace
{
    // Record header flag: record data is a u32 decompressed-size + zlib stream.
    constexpr std::uint32_t kCompressedFlag = 0x00040000;

    // 4-char signatures as little-endian u32 (how they read out of the file).
    constexpr std::uint32_t kSigGRUP = 0x50555247;  // 'GRUP'
    constexpr std::uint32_t kSigPNDT = 0x54444E50;  // 'PNDT'
    constexpr std::uint32_t kSigPPBD = 0x44425050;  // 'PPBD'
    constexpr std::uint32_t kSigXXXX = 0x58585858;  // 'XXXX' (large-subrecord size override)

    constexpr std::uint32_t kMaxListLen = 0x10000;  // sanity bound on a PPBD sub-array

    // Resolve <game-root>/Data/Starfield.esm from the running executable path.
    std::filesystem::path ResolveEsmPath()
    {
        wchar_t buf[MAX_PATH] {};
        const auto n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return {};
        return std::filesystem::path {buf}.parent_path() / L"Data" / L"Starfield.esm";
    }

    // Bounds-checked little-endian cursor over a byte buffer.
    struct Cursor
    {
        const std::uint8_t* p;
        std::size_t         size;
        std::size_t         off {0};

        bool          can(std::size_t n) const { return n <= size && off <= size - n; }
        std::uint32_t u32()
        {
            std::uint32_t v;
            std::memcpy(&v, p + off, sizeof v);
            off += sizeof v;
            return v;
        }
    };

    // Parse one PPBD payload (a single biome) and append its flora + fauna FormIDs.
    //   u32 biome | u32 chance | u32 unk | u32 RSGD
    //   u32 nFauna | nFauna x u32 (NPC_)
    //   u32 nKw    | nKw    x u32 (KYWD)
    //   u32 nFlora | u32 entrySize(>=9) | nFlora x { u32 FLOR, u32 MISC, u8 freq, pad to entrySize }
    void ParsePpbd(const std::uint8_t* data, std::size_t size, std::vector<std::uint32_t>& out)
    {
        Cursor c {data, size};
        if (!c.can(16))
            return;
        c.off += 16;  // biome, chance, unk, resource-gen

        if (!c.can(4))
            return;
        const auto nFauna = c.u32();
        if (nFauna > kMaxListLen || !c.can(static_cast<std::size_t>(nFauna) * 4))
            return;
        for (std::uint32_t i = 0; i < nFauna; ++i)
            if (const auto f = c.u32(); f != 0)
                out.push_back(f);

        if (!c.can(4))
            return;
        const auto nKw = c.u32();
        if (nKw > kMaxListLen || !c.can(static_cast<std::size_t>(nKw) * 4))
            return;
        c.off += static_cast<std::size_t>(nKw) * 4;  // skip keywords

        if (!c.can(8))
            return;
        const auto nFlora    = c.u32();
        auto       entrySize = c.u32();
        if (entrySize < 9)
            entrySize = 9;
        if (nFlora > kMaxListLen || !c.can(static_cast<std::size_t>(nFlora) * entrySize))
            return;
        for (std::uint32_t i = 0; i < nFlora; ++i)
        {
            std::uint32_t flor;
            std::memcpy(&flor, c.p + c.off, sizeof flor);  // first FormID in the entry
            if (flor != 0)
                out.push_back(flor);
            c.off += entrySize;
        }
    }

    // Walk subrecords in a record's (decompressed) data; call fn(sig, payload, size).
    template <typename Fn>
    void ForEachSubrecord(const std::uint8_t* data, std::size_t size, Fn&& fn)
    {
        std::size_t   i        = 0;
        std::uint32_t realSize = 0;
        bool          haveReal = false;
        while (i + 6 <= size)
        {
            std::uint32_t sig;
            std::uint16_t sz;
            std::memcpy(&sig, data + i, 4);
            std::memcpy(&sz, data + i + 4, 2);
            i += 6;
            if (sig == kSigXXXX)
            {
                if (i + 4 > size)
                    break;
                std::memcpy(&realSize, data + i, 4);
                haveReal = true;
                i += sz;
                continue;
            }
            std::size_t dsz = sz;
            if (haveReal)
            {
                dsz      = realSize;
                haveReal = false;
            }
            if (dsz > size - i)
                break;
            fn(sig, data + i, dsz);
            i += dsz;
        }
    }

    // Parse the PNDT top-level group's records into the map.
    void ParsePndtGroup(const std::vector<std::uint8_t>& group, Esm::PlanetSpeciesMap& map)
    {
        std::vector<std::uint8_t> decomp;
        std::size_t               i = 0;
        while (i + 24 <= group.size())
        {
            const std::uint8_t* rec = group.data() + i;
            std::uint32_t       sig, size, flags, formid;
            std::memcpy(&sig, rec + 0, 4);
            std::memcpy(&size, rec + 4, 4);
            std::memcpy(&flags, rec + 8, 4);
            std::memcpy(&formid, rec + 12, 4);
            i += 24;
            if (sig == kSigGRUP)
            {
                i += (size < 24) ? 0 : size - 24;  // nested group: skip its body
                continue;
            }
            if (size > group.size() - i)
                break;
            const std::uint8_t* data     = rec + 24;
            std::size_t         dataSize = size;
            i += size;
            if (sig != kSigPNDT)
                continue;

            if (flags & kCompressedFlag)
            {
                if (size < 4)
                    continue;
                std::uint32_t decompSize;
                std::memcpy(&decompSize, data, 4);
                decomp.assign(decompSize, 0);
                uLongf destLen = decompSize;
                if (uncompress(decomp.data(), &destLen,
                               reinterpret_cast<const Bytef*>(data + 4), size - 4) != Z_OK)
                    continue;
                data     = decomp.data();
                dataSize = destLen;
            }

            std::vector<std::uint32_t> species;
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig == kSigPPBD)
                    ParsePpbd(p, sz, species);
            });
            if (species.empty())
                continue;  // barren / resource-only body — nothing to add
            std::sort(species.begin(), species.end());
            species.erase(std::unique(species.begin(), species.end()), species.end());
            map.emplace(formid, std::move(species));
        }
    }

    void BuildMap(Esm::PlanetSpeciesMap& map)
    {
        const auto path = ResolveEsmPath();
        if (path.empty())
        {
            spdlog::warn("EsmReader: could not resolve Starfield.esm path");
            return;
        }
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            spdlog::warn("EsmReader: failed to open {}", path.string());
            return;
        }

        std::uint8_t hdr[24];
        f.read(reinterpret_cast<char*>(hdr), 24);
        if (!f || std::memcmp(hdr, "TES4", 4) != 0)
        {
            spdlog::warn("EsmReader: not a TES4 plugin");
            return;
        }
        std::uint32_t tes4Size;
        std::memcpy(&tes4Size, hdr + 4, 4);
        f.seekg(tes4Size, std::ios::cur);  // skip TES4 record data

        // Skip-scan the top-level groups for the PNDT type-group.
        while (f.read(reinterpret_cast<char*>(hdr), 24))
        {
            if (std::memcmp(hdr, "GRUP", 4) != 0)
                break;
            std::uint32_t gsize, label, gtype;
            std::memcpy(&gsize, hdr + 4, 4);
            std::memcpy(&label, hdr + 8, 4);
            std::memcpy(&gtype, hdr + 12, 4);
            if (gsize < 24)
                break;
            if (gtype == 0 && label == kSigPNDT)
            {
                std::vector<std::uint8_t> group(gsize - 24);
                if (f.read(reinterpret_cast<char*>(group.data()), group.size()))
                    ParsePndtGroup(group, map);
                break;
            }
            f.seekg(gsize - 24, std::ios::cur);
        }

        std::size_t speciesCount = 0;
        for (const auto& [_, v] : map)
            speciesCount += v.size();
        spdlog::info("EsmReader: loaded {} biome planets, {} species refs from Starfield.esm",
                     map.size(), speciesCount);
    }
}

namespace Esm
{
    const PlanetSpeciesMap& GetPlanetSpecies()
    {
        static PlanetSpeciesMap map;
        static std::once_flag   once;
        std::call_once(once, [] { BuildMap(map); });
        return map;
    }
}
