@echo off
setlocal

set "PROJECT_DIR=%~dp0"
set "DIST_DIR=%PROJECT_DIR%dist"
set "ZIP_NAME=Complete Planet Survey-1.0.0.zip"

echo --- Packaging Complete Planet Survey ---

if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%\fomod"
mkdir "%DIST_DIR%\Scripts\Source\User"

copy "%PROJECT_DIR%fomod\info.xml" "%DIST_DIR%\fomod\" >nul
copy "%PROJECT_DIR%fomod\ModuleConfig.xml" "%DIST_DIR%\fomod\" >nul
copy "%PROJECT_DIR%Data\Scripts\CompletePlanetSurveyQuest.pex" "%DIST_DIR%\Scripts\" >nul
copy "%PROJECT_DIR%Data\Scripts\Source\User\CompletePlanetSurveyQuest.psc" "%DIST_DIR%\Scripts\Source\User\" >nul

if exist "%PROJECT_DIR%%ZIP_NAME%" del "%PROJECT_DIR%%ZIP_NAME%"
powershell -Command "Compress-Archive -Path '%DIST_DIR%\*' -DestinationPath '%PROJECT_DIR%%ZIP_NAME%'"

echo --- Done: %ZIP_NAME% ---
