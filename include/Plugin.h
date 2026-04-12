#pragma once

namespace Plugin
{
    using namespace std::string_view_literals;
    static constexpr auto Name{ "CompletePlanetSurvey"sv };
    static constexpr auto Author{ "pjmagee"sv };
    static constexpr auto Version{ REL::Version{ 1, 0, 0, 0 } };
}

SFSE_PLUGIN_VERSION = []() noexcept {
    SFSE::PluginVersionData data{};

    data.PluginVersion(Plugin::Version);
    data.PluginName(Plugin::Name);
    data.AuthorName(Plugin::Author);
    data.UsesAddressLibrary(true);
    data.HasNoStructUse(true);
    data.CompatibleVersions({ SFSE::RUNTIME_LATEST });

    return data;
}();
