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

:: Deploy to game
if not exist "%GAME_DIR%\Data\Scripts" mkdir "%GAME_DIR%\Data\Scripts"
copy /Y "%PROJECT_DIR%Data\Scripts\CompletePlanetSurveyNative.pex" "%GAME_DIR%\Data\Scripts\" >nul
copy /Y "%PROJECT_DIR%Data\Scripts\CompletePlanetSurveyQuest.pex"  "%GAME_DIR%\Data\Scripts\" >nul

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

echo [OK] Deployed to %GAME_DIR%\Data\
