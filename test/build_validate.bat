@echo off
setlocal enabledelayedexpansion

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 ( echo [FAIL] MSVC x64 env & exit /b 1 )

cd /d "%~dp0\.."

set "XPKG=%LOCALAPPDATA%\.xmake\packages"
set "ZLIB="
for /d %%D in ("%XPKG%\z\zlib\*") do for /d %%H in ("%%D\*") do set "ZLIB=%%H"
if not defined ZLIB ( echo [FAIL] zlib package not found under %XPKG% & exit /b 1 )
echo [info] zlib = !ZLIB!

if not exist build\test mkdir build\test
cl /nologo /std:c++latest /EHsc /MD /DNOMINMAX /I test\stub /I include /I "!ZLIB!\include" test\ValidateMarkers.cpp "!ZLIB!\lib\zlib.lib" /Fe:build\test\ValidateMarkers.exe /Fo:build\test\ValidateMarkers.obj
if errorlevel 1 ( echo [FAIL] compile & exit /b 1 )

if not defined CPS_ESM_PATH set "CPS_ESM_PATH=E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
echo [info] CPS_ESM_PATH = !CPS_ESM_PATH!
build\test\ValidateMarkers.exe
exit /b %errorlevel%
