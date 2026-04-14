#!/usr/bin/env python3
"""
Build the Vortex/FOMOD-compatible distributable zip for Complete Planet Survey.

ZIP layout expected by ModuleConfig.xml:
  fomod/
    ModuleConfig.xml
    info.xml
  CompletePlanetSurvey.esm          -> Data/CompletePlanetSurvey.esm
  Scripts/
    CompletePlanetSurveyNative.pex  -> Data/Scripts/
    CompletePlanetSurveyQuest.pex   -> Data/Scripts/
  SFSE/
    Plugins/
      CompletePlanetSurvey.dll      -> Data/SFSE/Plugins/
"""
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))

SOURCES = [
    # (source path relative to ROOT,          zip entry path)
    ("fomod/ModuleConfig.xml",                 "fomod/ModuleConfig.xml"),
    ("fomod/info.xml",                         "fomod/info.xml"),
    ("Data/CompletePlanetSurvey.esm",          "CompletePlanetSurvey.esm"),
    ("Data/Scripts/CompletePlanetSurveyNative.pex", "Scripts/CompletePlanetSurveyNative.pex"),
    ("Data/Scripts/CompletePlanetSurveyQuest.pex",  "Scripts/CompletePlanetSurveyQuest.pex"),
    ("build/windows/x64/releasedbg/CompletePlanetSurvey.dll",
                                               "SFSE/Plugins/CompletePlanetSurvey.dll"),
]

out_path = os.path.join(ROOT, "CompletePlanetSurvey.zip")

missing = [src for src, _ in SOURCES if not os.path.exists(os.path.join(ROOT, src))]
if missing:
    print("[FAIL] Missing files:")
    for f in missing:
        print(f"  {f}")
    sys.exit(1)

with zipfile.ZipFile(out_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
    for src_rel, zip_entry in SOURCES:
        src_abs = os.path.join(ROOT, src_rel)
        zf.write(src_abs, zip_entry)
        print(f"  + {zip_entry}")

size_kb = os.path.getsize(out_path) / 1024
print(f"\n[OK] {out_path}  ({size_kb:.1f} KB)")
print("     Install via Vortex: Mods -> Install From File")
