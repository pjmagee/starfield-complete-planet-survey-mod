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

:: Capture build output so we can detect compile/link errors that xmake sometimes does NOT
:: surface via its exit code (it can return 0 after a failed compile by re-linking stale .obj ?
:: this silently shipped a stale DLL and cost a whole debugging session). Fail loud on any
:: "): error Cxxxx" / "error LNK" line, and verify the DLL was actually produced.
set "BUILDLOG=%TEMP%\cps_build_%RANDOM%.log"
xmake -y > "%BUILDLOG%" 2>&1
set "XMAKE_RC=%errorlevel%"
type "%BUILDLOG%"
findstr /R /C:": error [A-Z]" "%BUILDLOG%" >nul
if not errorlevel 1 ( echo [FAIL] compile/link errors detected in build output & del "%BUILDLOG%" 2>nul & exit /b 1 )
del "%BUILDLOG%" 2>nul
if not "%XMAKE_RC%"=="0" ( echo [FAIL] xmake build failed ^(rc=%XMAKE_RC%^) & exit /b 1 )
if not exist "build\windows\x64\releasedbg\CompletePlanetSurvey.dll" ( echo [FAIL] DLL not produced ^(compile failed^) & exit /b 1 )

echo [OK] Built build\windows\x64\releasedbg\CompletePlanetSurvey.dll
