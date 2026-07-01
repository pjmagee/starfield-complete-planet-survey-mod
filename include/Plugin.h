#pragma once

namespace Plugin
{
    using namespace std::string_view_literals;
    static constexpr auto Name{ "CompletePlanetSurvey"sv };
    static constexpr auto Author{ "pjmagee"sv };
    static constexpr auto Version{ REL::Version{ 1, 3, 0, 0 } };
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
    data.CompatibleVersions({ SFSE::RUNTIME_LATEST });

    return data;
}();
