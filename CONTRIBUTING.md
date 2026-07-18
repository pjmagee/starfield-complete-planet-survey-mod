# Contributing

Thanks for interest in **Complete Planet Survey**. Player support and releases live on
[Nexus Mods](https://www.nexusmods.com/starfield/mods/16493). This repo is the SFSE plugin
source, Papyrus scripts, and ESM.

## Before you open an issue

1. Confirm you are on a **supported** Starfield + SFSE + Address Library set for the mod
   version (see [CHANGELOG.md](CHANGELOG.md)).
2. Capture `Documents/My Games/Starfield/SFSE/Logs/CompletePlanetSurvey.log` **before** the
   next game launch (the log is truncated each run).
3. Search existing issues for a duplicate.

## Bug reports

Use the **Bug report** issue form. Include:

- Game / SFSE / mod version
- Whether Hand Scanner / Orbital Scanner toggles are on
- Exact console command or scan path (land vs star map)
- Relevant log lines and a short repro

## Pull requests

1. Target `main`.
2. Keep PRs focused (one fix or feature).
3. Build with `build.bat` (releasedbg). After native or Papyrus changes, run
   `test\build_validate.bat` when the ESM reader or markers are touched.
4. Do not commit game install paths, secrets, or large binary dumps under `re/` unless
   agreed.
5. Engine Address Library IDs and layouts that belong in CommonLibSF should go to
   [libxse/commonlibsf](https://github.com/libxse/commonlibsf) (or a fork PR) when possible;
   the mod should consume named `RE::ID` / layout helpers rather than invent parallel
   registries long term.
6. Fill out the pull request template checklist.

### Local build

```bat
build.bat
deploy.bat
```

See [CLAUDE.md](CLAUDE.md) for layout, console commands, and known gotchas.

## Code of Conduct

Participation is governed by [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## License

Contributions are under the same terms as [LICENSE](LICENSE) (GPL-3.0-or-later with the
modding exceptions described there, consistent with CommonLibSF).
