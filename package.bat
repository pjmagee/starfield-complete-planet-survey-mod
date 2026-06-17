@echo off
:: Build the distributable zip via package.py (the single source of truth for the
:: ZIP layout; CI runs the same script on v* tags). Packages the CURRENT build
:: artifacts, so run build.bat first. Pass a version as %1, else "dev".
::   package.bat            -> Complete-Planet-Survey-dev.zip
::   package.bat 1.0.8      -> Complete-Planet-Survey-1.0.8.zip
setlocal

set "VER=%~1"
if "%VER%"=="" set "VER=dev"

cd /d "%~dp0"
python package.py --version %VER%
if errorlevel 1 ( echo [FAIL] package.py failed & exit /b 1 )
