@echo off
setlocal

set "GAME_DIR=E:\SteamLibrary\steamapps\common\Starfield"
set "PROJECT_DIR=%~dp0"

:: Compile Papyrus
set "COMPILER=E:\SteamLibrary\steamapps\common\Starfield 2722710\Tools\Papyrus Compiler\PapyrusCompiler.exe"
set "INC=%PROJECT_DIR%temp_scripts\source;%PROJECT_DIR%Data\Scripts\Source\User"
set "FLG=%PROJECT_DIR%temp_scripts\Starfield_Papyrus_Flags.flg"
set "OUT=%PROJECT_DIR%Data\Scripts"

"%COMPILER%" "CompletePlanetSurveyNative" -i="%INC%" -f="%FLG%" -o="%OUT%"
if errorlevel 1 ( echo Native compile failed & exit /b 1 )

"%COMPILER%" "CompletePlanetSurveyQuest" -i="%INC%" -f="%FLG%" -o="%OUT%"
if errorlevel 1 ( echo Quest compile failed & exit /b 1 )

:: ESM is authored in Creation Kit and lives at Data\CompletePlanetSurvey.esm.
:: To pull a fresh CK save back into the repo, run import-esm.bat first.

:: Deploy to game
if not exist "%GAME_DIR%\Data\Scripts" mkdir "%GAME_DIR%\Data\Scripts"
copy /Y "%PROJECT_DIR%Data\Scripts\CompletePlanetSurveyNative.pex" "%GAME_DIR%\Data\Scripts\" >nul
copy /Y "%PROJECT_DIR%Data\Scripts\CompletePlanetSurveyQuest.pex"  "%GAME_DIR%\Data\Scripts\" >nul

:: Remove old .esp if present (replaced by .esm)
if exist "%GAME_DIR%\Data\CompletePlanetSurvey.esp" (
  del /Q "%GAME_DIR%\Data\CompletePlanetSurvey.esp" >nul
  echo [OK] Removed old CompletePlanetSurvey.esp
)

:: Deploy ESM (Settings > Gameplay toggle)
copy /Y "%PROJECT_DIR%Data\CompletePlanetSurvey.esm" "%GAME_DIR%\Data\" >nul
if errorlevel 1 ( echo [FAIL] ESM copy failed & exit /b 1 )
echo [OK] Deployed ESM

:: Deploy SFSE plugin DLL
set "DLL=%PROJECT_DIR%build\windows\x64\releasedbg\CompletePlanetSurvey.dll"
if exist "%DLL%" (
  if not exist "%GAME_DIR%\Data\SFSE\Plugins" mkdir "%GAME_DIR%\Data\SFSE\Plugins"
  copy /Y "%DLL%" "%GAME_DIR%\Data\SFSE\Plugins\" >nul
  if errorlevel 1 ( echo [FAIL] DLL copy failed ^(Starfield still running?^) & exit /b 1 )
  echo [OK] Deployed DLL
) else (
  echo [WARN] DLL not found at %DLL%
)

:: Ensure plugin is enabled in plugins.txt (prefix * = active)
:: Remove any old .esp entry; add/keep .esm entry
set "PLUGINS=%LOCALAPPDATA%\Starfield\plugins.txt"
if exist "%PLUGINS%" (
  findstr /v /i /c:"CompletePlanetSurvey.esp" "%PLUGINS%" > "%PLUGINS%.tmp" 2>nul
  move /Y "%PLUGINS%.tmp" "%PLUGINS%" >nul

  findstr /i /c:"*CompletePlanetSurvey.esm" "%PLUGINS%" >nul 2>&1
  if errorlevel 1 (
    findstr /v /i /c:"CompletePlanetSurvey.esm" "%PLUGINS%" > "%PLUGINS%.tmp" 2>nul
    echo *CompletePlanetSurvey.esm>> "%PLUGINS%.tmp"
    move /Y "%PLUGINS%.tmp" "%PLUGINS%" >nul
    echo [OK] Added *CompletePlanetSurvey.esm to plugins.txt
  ) else (
    echo [OK] plugins.txt already has *CompletePlanetSurvey.esm
  )
) else (
  echo [WARN] plugins.txt not found at %PLUGINS%
)

echo [OK] Deployed to %GAME_DIR%\Data\
