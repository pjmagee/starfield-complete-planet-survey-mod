@echo off
:: Build the SFSE plugin DLL via xmake (this project's build system).
:: Output: build\windows\x64\releasedbg\CompletePlanetSurvey.dll  (consumed by deploy.bat).
:: vcvarsall is loaded so xmake's dependency builds find the MSVC compiler.
setlocal

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 ( echo [FAIL] Could not initialize MSVC x64 environment & exit /b 1 )

cd /d "%~dp0"

xmake f -m releasedbg -y
if errorlevel 1 ( echo [FAIL] xmake configure failed & exit /b 1 )

xmake -y
if errorlevel 1 ( echo [FAIL] xmake build failed & exit /b 1 )

echo [OK] Built build\windows\x64\releasedbg\CompletePlanetSurvey.dll
