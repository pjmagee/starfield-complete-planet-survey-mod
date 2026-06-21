-- set minimum xmake version
set_xmakever("3.0.0")

-- set project
set_project("CompletePlanetSurvey")
set_version("1.1.0")
set_arch("x64")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

-- add rules
add_rules("mode.debug", "mode.releasedbg")

-- zlib: inflate compressed PNDT records when reading Starfield.esm at runtime
add_requires("zlib")

-- include commonlibsf
includes("extern/CommonLibSF")

-- plugin target
target("CompletePlanetSurvey", function()
    -- add commonlibsf deps
    add_deps("commonlibsf")
    add_packages("zlib")
    add_rules("commonlib.plugin")

    -- plugin metadata
    set_license("MIT")
    on_config(function(target)
        target:data_set("commonlib.plugin.config", {
            author = "pjmagee",
            name = "CompletePlanetSurvey",
            description = "Complete Planet Survey - auto-complete planet surveys",
        })
    end)

    -- Catch C++ exceptions AND structured faults (access violations) at the Papyrus
    -- boundary: every bound native is wrapped in try/catch (GuardedNative in src/Main.cpp).
    -- /EHa lets catch(...) trap SEH, so a wrong/garbage offset deref logs + disables the
    -- feature instead of a silent CTD crossing into the Papyrus VM.
    add_cxflags("/EHa")

    -- source files
    add_files("src/**.cpp")
    add_includedirs("include")
    add_headerfiles("include/**.h")

    -- precompiled header
    set_pcxxheader("include/PCH.h")

    -- install to Data/sfse/plugins
    add_installfiles("$(buildir)/$(plat)/$(arch)/$(mode)/CompletePlanetSurvey.dll", { prefixdir = "sfse/plugins" })
end)
