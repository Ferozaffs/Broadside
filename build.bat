@echo off
rem Builds Broadside with the CMake + Ninja bundled in Visual Studio 2026.
setlocal
set VSROOT=C:\Program Files\Microsoft Visual Studio\18\Community
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set CMAKE="%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
cd /d "%~dp0"
if not exist build\build.ninja (
    %CMAKE% -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=%NINJA%" || exit /b 1
)
%CMAKE% --build build || exit /b 1
echo.
echo Built: %~dp0build\broadside.exe
