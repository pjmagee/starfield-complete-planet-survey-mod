@echo off
:: Copy the ESM from the game's Data folder back into the repo.
:: Run after editing/saving CompletePlanetSurvey.esm in Creation Kit, since
:: CK can only save into the active Starfield Data directory.
setlocal

set "GAME_ESM=E:\SteamLibrary\steamapps\common\Starfield\Data\CompletePlanetSurvey.esm"
set "REPO_ESM=%~dp0Data\CompletePlanetSurvey.esm"

if not exist "%GAME_ESM%" (
  echo [FAIL] No ESM at %GAME_ESM%
  exit /b 1
)

:: CK may also have written a stray .esp from a botched save — warn loudly.
if exist "%GAME_ESM:.esm=.esp%" (
  echo [WARN] CompletePlanetSurvey.esp also present in game Data — delete it before launching Starfield ^(loading both crashes the game^).
)

copy /Y "%GAME_ESM%" "%REPO_ESM%" >nul
if errorlevel 1 ( echo [FAIL] Copy failed & exit /b 1 )
echo [OK] Imported ESM from game -> repo
