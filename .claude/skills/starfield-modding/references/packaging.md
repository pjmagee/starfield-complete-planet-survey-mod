# Packaging & Distribution

## Directory layout expected by users

```
MyMod/
├── Data/
│   ├── MyMod.esp                      (or .esm / .esl)
│   ├── MyMod - Main.ba2               (optional archive)
│   ├── Scripts/
│   │   └── MyMod_Quest.pex
│   ├── Scripts/Source/User/
│   │   └── MyMod_Quest.psc
│   └── SFSE/Plugins/
│       ├── MyMod.dll
│       └── MyMod.log                  (written at runtime, not shipped)
└── fomod/
    ├── info.xml
    └── ModuleConfig.xml
```

Mod managers (Vortex, MO2, Wrye Bash BAIN) install from the archive root, so the top-level should be `Data/` (plus `fomod/` if present). A zip that drops files at the root forces users to fix paths manually.

## FOMOD — option-driven installer

Two files under `fomod/`:

- `info.xml` — metadata (name, author, version, website)
- `ModuleConfig.xml` — steps, groups, and which files each option copies

Skeleton:

```xml
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
        xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">
    <moduleName>My Mod</moduleName>
    <installSteps order="Explicit">
        <installStep name="Main">
            <optionalFileGroups order="Explicit">
                <group name="Version" type="SelectExactlyOne">
                    <plugins order="Explicit">
                        <plugin name="Standard">
                            <description>Base version</description>
                            <files>
                                <folder source="standard" destination=""/>
                            </files>
                            <typeDescriptor><type name="Recommended"/></typeDescriptor>
                        </plugin>
                        <plugin name="Lite">
                            <description>Reduced scope</description>
                            <files>
                                <folder source="lite" destination=""/>
                            </files>
                            <typeDescriptor><type name="Optional"/></typeDescriptor>
                        </plugin>
                    </plugins>
                </group>
            </optionalFileGroups>
        </installStep>
    </installSteps>
</config>
```

`source` paths are relative to the archive root; `destination=""` means "install into the game's Data folder as-is" (mod managers treat it that way).

See this repo's [fomod/](../../../fomod/) for a working example.

## Versioning

- Bump the SFSE plugin's `MAKE_VERSION(maj, min, patch)` whenever the DLL ships.
- Bump the FOMOD `info.xml` `<Version>` to match.
- Tag git with `vX.Y.Z` so source and release line up.
- Add new supported runtimes to `compatibleVersions` — do not silently widen to unknown builds.

## Nexus Mods submission checklist

1. Archive root contains `Data/` (and `fomod/` if used) — not a wrapping folder.
2. README.md or description covers: dependencies (SFSE version, Address Library), known conflicts, uninstall steps.
3. License: clear, up front. MIT/CC-BY is common for source; "all rights reserved — ask before reuploading" for content.
4. Source on GitHub linked from the Nexus page for DLL mods (transparency + security review).
5. Changelog entry per release.

## What belongs where

| Target audience | Repo | Notes |
|---|---|---|
| Players | Nexus Mods | Packaged archive, FOMOD, screenshots |
| Other authors / contributors | GitHub | Source, build instructions, issues |
| Creators (official) | Bethesda Creations | Separate pipeline; Creation Kit only |
