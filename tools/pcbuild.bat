@echo off
setlocal enabledelayedexpansion
rem Build the PC port. Called by build-debug.bat and build-release.bat at the
rem repository root; run those rather than this. Adapted from treedome's.
rem
rem   tools\pcbuild.bat <Debug|Release> [backends] [arch]
rem
rem       backends: D3D9, D3D11 and GL3 in one executable by default. Name a
rem                 list (D3D11;GL3) or one (GL3, NULL) to build only that.
rem       arch:     x64 (default) or x86
rem
rem Each configuration builds into its own directory (the backend set and the
rem word size are baked into the CMake cache), and each puts tssm.exe and the
rem DLLs it needs into bin\. The last build wins; bin\BUILD-INFO.txt says which.
rem
rem The game's files are not part of the build. Point config.ini's [assets] path
rem (written beside tssm.exe on the first run), or TSSM_ASSETS, at your own
rem extracted Xbox disc.

set "CONFIG=%~1"
set "BACKEND=%~2"
set "ARCH=%~3"
if "%CONFIG%"=="" set "CONFIG=Debug"
set "BACKENDARG=-UTSSM_RENDER_BACKENDS"
if not "%BACKEND%"=="" set "BACKENDARG=-DTSSM_RENDER_BACKENDS=%BACKEND%"
if "%ARCH%"=="" set "ARCH=x64"
if /i not "%ARCH%"=="x86" if /i not "%ARCH%"=="x64" (
  echo ERROR: arch must be x64 or x86, not %ARCH%.
  exit /b 1
)
set "M32=OFF"
set "SUFFIX="
if /i "%ARCH%"=="x86" (set "M32=ON" & set "SUFFIX=-x86")

set "ROOT=%~dp0.."
pushd "%ROOT%" || exit /b 1
set "ROOT=%CD%"

set "BUILDDIR=build-%CONFIG%"
if /i "%CONFIG%"=="Debug"   set "BUILDDIR=build-debug"
if /i "%CONFIG%"=="Release" set "BUILDDIR=build-release"
set "BUILDDIR=%BUILDDIR%%SUFFIX%"

rem ---- the MSVC environment --------------------------------------------------
rem clang++ links against the MSVC libraries and the Windows SDK of the same
rem architecture. vswhere says where Visual Studio (or the Build Tools) is.
if defined VSCMD_ARG_TGT_ARCH (
  if /i "%VSCMD_ARG_TGT_ARCH%"=="%ARCH%" goto :have_env
)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe not found. Install the Visual Studio 2022 Build Tools.
  popd & exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
  echo ERROR: no Visual Studio installation found.
  popd & exit /b 1
)
rem The path is quoted in the message on purpose: the Build Tools live under
rem "Program Files (x86)", and an unquoted ")" inside this block ends it.
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" (
  echo ERROR: "%VSPATH%" has no C++ toolchain ^(vcvarsall.bat missing^).
  popd & exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
if errorlevel 1 (echo ERROR: vcvarsall %ARCH% failed. & popd & exit /b 1)
:have_env

rem ---- tools -------------------------------------------------------------------
rem LLVM and CMake where winget installs them, if they are not on PATH already.
where clang++ >nul 2>&1 || if exist "%ProgramFiles%\LLVM\bin\clang++.exe" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
where cmake >nul 2>&1 || if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "PATH=%ProgramFiles%\CMake\bin;%PATH%"
where clang++ >nul 2>&1 || (echo ERROR: clang++ is not on PATH. & popd & exit /b 1)
where cmake   >nul 2>&1 || (echo ERROR: cmake is not on PATH. & popd & exit /b 1)
where ninja   >nul 2>&1 || (echo ERROR: ninja is not on PATH. & popd & exit /b 1)

rem ---- FFmpeg (optional) -----------------------------------------------------
rem vcpkg's ffmpeg for this architecture. Without it movies are skipped.
if not defined TSSM_VCPKG set "TSSM_VCPKG=%USERPROFILE%\vcpkg\installed\%ARCH%-windows"
set "PREFIX="
if exist "%TSSM_VCPKG%\include" (
  set "PREFIX=-DCMAKE_PREFIX_PATH=%TSSM_VCPKG:\=/%"
) else (
  echo NOTE: %TSSM_VCPKG% not found -- building without FFmpeg, so no movies.
  echo       Set TSSM_VCPKG if it lives elsewhere.
)

rem ---- the app icon (optional) -------------------------------------------------
rem TSSM_ICON_DIR: a folder from tools\make_icons.ps1 (docs\ICON.md). Unset, the
rem build directory keeps whatever it was configured with (none by default).
set "ICONARG="
if defined TSSM_ICON_DIR set "ICONARG="-DTSSM_ICON_DIR=%TSSM_ICON_DIR:\=/%""

rem ---- configure and build ---------------------------------------------------
echo === %CONFIG% / %ARCH% -^> %BUILDDIR%, exe into bin\ ===
cmake -S . -B "%BUILDDIR%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=%CONFIG% ^
  -DCMAKE_C_COMPILER=clang ^
  -DCMAKE_CXX_COMPILER=clang++ ^
  %BACKENDARG% ^
  -DTSSM_BUILD_32BIT=%M32% ^
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="%ROOT:\=/%/bin" ^
  %PREFIX% %ICONARG%
if errorlevel 1 (echo. & echo CONFIGURE FAILED & popd & exit /b 1)

cmake --build "%BUILDDIR%"
if errorlevel 1 (echo. & echo BUILD FAILED -- tools\pcprogress.py lists what does not compile yet & popd & exit /b 1)

set "BACKENDNAME=?"
for /f "tokens=2 delims==" %%i in ('findstr /b "TSSM_RENDER_BACKENDS:" "%BUILDDIR%\CMakeCache.txt"') do set "BACKENDNAME=%%i"
> "%ROOT%\bin\BUILD-INFO.txt" echo %CONFIG% / %BACKENDNAME% / %ARCH%, built from %BUILDDIR%
echo.
echo === bin\tssm.exe is now %CONFIG% / %BACKENDNAME% / %ARCH% ===
if not exist "%ROOT%\bin\config.ini" (
  echo     No bin\config.ini yet. The game writes one with the defaults on
  echo     first run; set [assets] path in it to your extracted Xbox disc.
)
popd
endlocal
