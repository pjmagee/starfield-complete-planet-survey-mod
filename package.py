#!/usr/bin/env python3
"""
Build the Vortex/FOMOD-compatible distributable zip for Complete Planet Survey.

Single source of truth for the ZIP layout — CI (`.github/workflows/build.yml`)
invokes this script instead of duplicating the file-copy manifest.

Expected ZIP layout (matches ModuleConfig.xml):
  fomod/
    ModuleConfig.xml
    info.xml
  CompletePlanetSurvey.esm          -> Data/CompletePlanetSurvey.esm
  Scripts/
    CompletePlanetSurveyNative.pex  -> Data/Scripts/
    CompletePlanetSurveyQuest.pex   -> Data/Scripts/
    Source/User/
      CompletePlanetSurveyNative.psc
      CompletePlanetSurveyQuest.psc
  SFSE/
    Plugins/
      CompletePlanetSurvey.dll      -> Data/SFSE/Plugins/
"""
import argparse
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))

SOURCES = [
    # (source path relative to ROOT, zip entry path)
    ("fomod/ModuleConfig.xml",                              "fomod/ModuleConfig.xml"),
    ("fomod/info.xml",                                      "fomod/info.xml"),
    ("Data/CompletePlanetSurvey.esm",                       "CompletePlanetSurvey.esm"),
    ("Data/Scripts/CompletePlanetSurveyNative.pex",         "Scripts/CompletePlanetSurveyNative.pex"),
    ("Data/Scripts/CompletePlanetSurveyQuest.pex",          "Scripts/CompletePlanetSurveyQuest.pex"),
    ("Data/Scripts/Source/User/CompletePlanetSurveyNative.psc",
                                                            "Scripts/Source/User/CompletePlanetSurveyNative.psc"),
    ("Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc",
                                                            "Scripts/Source/User/CompletePlanetSurveyQuest.psc"),
    ("build/windows/x64/releasedbg/CompletePlanetSurvey.dll",
                                                            "SFSE/Plugins/CompletePlanetSurvey.dll"),
]


def build(output: str) -> None:
    missing = [src for src, _ in SOURCES if not os.path.exists(os.path.join(ROOT, src))]
    if missing:
        print("[FAIL] Missing files:")
        for f in missing:
            print(f"  {f}")
        sys.exit(1)

    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for src_rel, zip_entry in SOURCES:
            zf.write(os.path.join(ROOT, src_rel), zip_entry)
            print(f"  + {zip_entry}")

    size_kb = os.path.getsize(output) / 1024
    print(f"\n[OK] {output}  ({size_kb:.1f} KB)")
    print("     Install via Vortex: Mods -> Install From File")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("--output", default=os.path.join(ROOT, "CompletePlanetSurvey.zip"),
                        help="Output zip path (default: CompletePlanetSurvey.zip in repo root)")
    parser.add_argument("--version",
                        help="Shorthand: output to Complete-Planet-Survey-<version>.zip in CWD")
    args = parser.parse_args()

    output = f"Complete-Planet-Survey-{args.version}.zip" if args.version else args.output
    build(output)
