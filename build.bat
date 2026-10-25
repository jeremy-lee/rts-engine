@echo off
rem RTS Engine - one-click build (MSVC + CMake + Ninja)
setlocal

set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist "%VS%\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS=D:\Program Files\Microsoft Visual Studio\2022\Community"
)
if not exist "%VS%\VC\Auxiliary\Build\vcvars64.bat" (
    echo [ERROR] vcvars64.bat not found. Set VS path manually.
    exit /b 1
)

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul

set "CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if not exist "build" mkdir build
cd /d "%~dp0"

"%CMAKE%" -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

"%CMAKE%" --build build
if errorlevel 1 exit /b 1

echo.
echo [BUILD OK] run: build\rts_app.exe  and  build\rts_tests.exe
