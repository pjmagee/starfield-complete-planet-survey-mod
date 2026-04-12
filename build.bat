@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
cd /d D:\Projects\pjmagee\starfield-complete-planet-survey-mod
cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
if errorlevel 1 ( echo CMake configure failed & exit /b 1 )
cmake --build build/release
if errorlevel 1 ( echo Build failed & exit /b 1 )
echo --- Build Complete ---
