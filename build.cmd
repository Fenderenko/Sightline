@echo off
rem Builds Sightline with MSVC (no CMake required).
rem Usage: build.cmd [release|debug]
setlocal EnableDelayedExpansion

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=release"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [build] vswhere.exe not found. Install Visual Studio 2022 Build Tools with the C++ workload.
  exit /b 1
)
rem Delayed expansion: the "(x86)" in the path would otherwise close the for clause.
for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo [build] MSVC x64 toolset not found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cd /d "%~dp0"
rem SL_OBJ / SL_BIN override the output folders (parallel builds); SL_TARGET=core skips settings.
set "OBJ=build\obj\%CONFIG%"
set "BIN=bin"
if defined SL_OBJ set "OBJ=%SL_OBJ%"
if defined SL_BIN set "BIN=%SL_BIN%"
for %%d in (common core settings) do if not exist "%OBJ%\%%d" mkdir "%OBJ%\%%d"
if not exist "%BIN%" mkdir "%BIN%"

set "CFLAGS=/nologo /std:c++20 /EHsc /W4 /permissive- /utf-8 /MP /Zi /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Isrc"
set "LFLAGS=/nologo /SUBSYSTEM:WINDOWS /DEBUG /MANIFEST:EMBED"
if /i "%CONFIG%"=="debug" (
  set "CFLAGS=%CFLAGS% /Od /MTd /D_DEBUG"
) else (
  set "CFLAGS=%CFLAGS% /O2 /MT /DNDEBUG /GL"
  set "LFLAGS=%LFLAGS% /LTCG /OPT:REF /OPT:ICF"
)
set "LIBS=user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib d3d11.lib dxgi.lib d2d1.lib dwrite.lib dcomp.lib windowscodecs.lib dwmapi.lib"

echo [build] common
cl %CFLAGS% /c src\common\*.cpp /Fo"%OBJ%\common\\" /Fd"%OBJ%\common\vc.pdb" || exit /b 1
echo [build] icon
if not exist "%OBJ%\tools" mkdir "%OBJ%\tools"
cl %CFLAGS% tools\mkicon.cpp "%OBJ%\common\*.obj" /Fo"%OBJ%\tools\\" /Fd"%OBJ%\tools\vc.pdb" /Fe"%OBJ%\tools\mkicon.exe" /link /SUBSYSTEM:CONSOLE %LIBS% >nul || exit /b 1
"%OBJ%\tools\mkicon.exe" "%OBJ%\app.ico" || exit /b 1
rc /nologo /i "%OBJ%" /fo "%OBJ%\app.res" res\app.rc || exit /b 1
echo [build] core
cl %CFLAGS% /c src\core\*.cpp /Fo"%OBJ%\core\\" /Fd"%OBJ%\core\vc.pdb" || exit /b 1
echo [build] link sightline.exe
rem The core imports no graphics stack at all; /OPT:REF drops the unused Direct2D logo code from shared objs.
link %LFLAGS% /OUT:"%BIN%\sightline.exe" "%OBJ%\core\*.obj" "%OBJ%\common\*.obj" "%OBJ%\app.res" %LIBS% || exit /b 1

rem SL_TEST=1: build and run the self-test (rendering symmetry, config round trip, share codes).
rem It needs only the common objects, so it also runs with SL_TARGET=core.
if "%SL_TEST%"=="1" (
  echo [build] self-test
  if not exist "%OBJ%\test" mkdir "%OBJ%\test"
  cl %CFLAGS% tests\selftest.cpp "%OBJ%\common\*.obj" /Fo"%OBJ%\test\\" /Fd"%OBJ%\test\vc.pdb" /Fe"%OBJ%\test\selftest.exe" /link /SUBSYSTEM:CONSOLE %LIBS% >nul || exit /b 1
  "%OBJ%\test\selftest.exe" "%OBJ%\test\presets.bmp" > "%OBJ%\test\selftest.txt" || exit /b 1
  type "%OBJ%\test\selftest.txt"
  findstr /c:"crooked presets: 0" "%OBJ%\test\selftest.txt" >nul || exit /b 1
  findstr /c:"parsed=1 meta=1 edited=1 sharecode=1" "%OBJ%\test\selftest.txt" >nul || exit /b 1
  findstr /c:"i18n gaps: 0" "%OBJ%\test\selftest.txt" >nul || exit /b 1
)

if /i "%SL_TARGET%"=="core" (
  echo [build] done: %BIN%\sightline.exe
  exit /b 0
)

echo [build] settings
cl %CFLAGS% /c src\settings\*.cpp /Fo"%OBJ%\settings\\" /Fd"%OBJ%\settings\vc.pdb" || exit /b 1
echo [build] link sightline-settings.exe
link %LFLAGS% /OUT:"%BIN%\sightline-settings.exe" "%OBJ%\settings\*.obj" "%OBJ%\common\*.obj" "%OBJ%\app.res" %LIBS% || exit /b 1

echo [build] done: %BIN%\sightline.exe, %BIN%\sightline-settings.exe
exit /b 0
