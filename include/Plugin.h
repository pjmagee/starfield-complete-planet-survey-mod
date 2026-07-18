#pragma once

namespace Plugin
{
    using namespace std::string_view_literals;
    static constexpr auto Name{ "CompletePlanetSurvey"sv };
    static constexpr auto Author{ "pjmagee"sv };
    static constexpr auto Version{ REL::Version{ 1, 5, 0, 0 } };
}

SFSE_PLUGIN_VERSION = []() noexcept {
    SFSE::PluginVersionData data{};

    data.PluginVersion(Plugin::Version);
    data.PluginName(Plugin::Name);
    data.AuthorName(Plugin::Author);
    data.UsesAddressLibrary(true);
    // This plugin reads version-specific engine struct layouts at hardcoded offsets, so it IS
    // layout-dependent — SFSE must refuse to load it on a runtime whose layout it wasn't built
    // for, rather than load and read wrong offsets. (Was incorrectly HasNoStructUse(true).)
    data.IsLayoutDependent(true);
    // Verified Starfield 1.16.236.0 / 1.16.242.0 / 1.16.244.0 — same REL::IDs and layout
    // offsets (see src/Main.cpp, re/ghidra/output/offset-skew-236-vs-244.md). Extend this
    // list when a new game build is re-verified; remove builds whose layouts diverge.
    data.CompatibleVersions({
        SFSE::RUNTIME_SF_1_16_236,
        SFSE::RUNTIME_SF_1_16_242,
        SFSE::RUNTIME_SF_1_16_244,
    });

    return data;
}();
