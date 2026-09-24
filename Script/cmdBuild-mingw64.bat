@echo off
setlocal enabledelayedexpansion

rem ============================================================
rem relayFile one-shot packaging script
rem Usage: cmdBuild.bat <cmake-build-dir>
rem   e.g. cmdBuild.bat D:\relayFile\build-release
rem Steps:
rem   1. Find relayFileGui.exe under the build dir, locate its folder
rem   2. Run windeployqt to deploy Qt runtime into that folder
rem   3. Copy repo "licenses" dir into that folder
rem   4. Parse version and commit from <build-dir>\relayFileVersion.h)
rem   5. Run makensis to build the installer into the build dir root
rem ============================================================

rem ---- Qt root used to locate windeployqt (override via env or edit here) ----
if not defined QT_LIB_ROOT set "QT_LIB_ROOT=C:\msys64\ucrt64\"

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
for %%i in ("%REPO_ROOT%") do set "REPO_ROOT=%%~fi"

rem ---- arg check ----
if "%~1"=="" (
    echo [ERROR] missing argument: cmake build dir
    echo Usage: %~nx0 ^<cmake-build-dir^>
    exit /b 1
)
set "BUILD_DIR=%~f1"
if "!BUILD_DIR:~-1!"=="\" set "BUILD_DIR=!BUILD_DIR:~0,-1!"
if not exist "%BUILD_DIR%" (
    echo [ERROR] build dir does not exist: !BUILD_DIR!
    exit /b 1
)

rem ---- 1. find relayFileGui.exe ----
set "GUI_EXE="
for /f "delims=" %%f in ('dir /s /b "%BUILD_DIR%\relayFileGui.exe" 2^>nul') do (
    if not defined GUI_EXE set "GUI_EXE=%%f"
)
if not defined GUI_EXE (
    echo [ERROR] relayFileGui.exe not found under: %BUILD_DIR%
    exit /b 1
)
for %%f in ("%GUI_EXE%") do set "BIN_DIR=%%~dpf"
if "!BIN_DIR:~-1!"=="\" set "BIN_DIR=!BIN_DIR:~0,-1!"
echo [1/5] bin dir: !BIN_DIR!

rem ---- 2. windeployqt: deploy Qt runtime next to the exe ----
set "WINDEPLOYQT=%QT_LIB_ROOT%\bin\windeployqt.exe"
if not exist "%WINDEPLOYQT%" (
    echo [ERROR] windeployqt not found: !WINDEPLOYQT!
    echo [HINT] check QT_LIB_ROOT (current: %QT_LIB_ROOT%^)
    exit /b 1
)
echo [2/5] windeployqt: %WINDEPLOYQT%
"%WINDEPLOYQT%" "%GUI_EXE%"
if errorlevel 1 (
    echo [ERROR] windeployqt failed
    exit /b 1
)

clangPack -e "%GUI_EXE%" -r -c C:\msys64\ucrt64\bin
if errorlevel 1 (
    echo [ERROR] clangPack failed
    exit /b 1
)

rem ---- 3. copy licenses ----
set "LICENSES_DIR=%REPO_ROOT%\licenses"
if exist "%LICENSES_DIR%\" (
    echo [3/5] copy licenses -^> !BIN_DIR!\licenses
    xcopy /e /i /y /q "%LICENSES_DIR%" "!BIN_DIR!\licenses\" >nul
) else (
    echo [WARN] licenses dir not found, skipped: %LICENSES_DIR%
)
if exist "%REPO_ROOT%\LICENSE" copy /y "%REPO_ROOT%\LICENSE" "!BIN_DIR!\licenses\" >nul

rem ---- 4. parse version and commit from relayFileVersion (unencrypted copy of relayFileVersion.h) ----
set "VERFILE=%BUILD_DIR%\relayFileVersion.h"
if not exist "%VERFILE%" (
    echo [ERROR] version file not found: !VERFILE!
    echo [HINT] copy relayFileVersion.h to relayFileVersion next to it
    exit /b 1
)
set "VERSION="
for /f "tokens=3" %%v in ('findstr /b /c:"#define VERSION_NUM" "%VERFILE%" 2^>nul') do set "VERSION=%%v"
if defined VERSION set "VERSION=%VERSION:"=%"
set "COMMIT="
for /f "tokens=3" %%h in ('findstr /b /c:"#define VERSION_GIT_COMMIT" "%VERFILE%" 2^>nul') do set "COMMIT=%%h"
if defined COMMIT set "COMMIT=%COMMIT:"=%"
if not defined VERSION (
    echo [ERROR] cannot parse VERSION_NUM from !VERFILE!
    exit /b 1
)
if not defined COMMIT (
    echo [ERROR] cannot parse VERSION_GIT_COMMIT from !VERFILE!
    exit /b 1
)
echo [4/5] version: %VERSION%  commit: %COMMIT%

rem ---- 5. build installer with makensis ----
set "NSIS_EXE=%ProgramFiles(x86)%\NSIS\makensis.exe"
if not exist "%NSIS_EXE%" (
    echo [ERROR] makensis not found: !NSIS_EXE!
    exit /b 1
)
echo [5/5] building installer ...
"%NSIS_EXE%" /DSRC_BIN_DIR="%BIN_DIR%" /DOUT_DIR="%BUILD_DIR%" /DPRODUCT_VERSION="%VERSION%" /DCOMMIT_ID="%COMMIT%" "%SCRIPT_DIR%relayFile.nsi"
if errorlevel 1 (
    echo [ERROR] packaging failed
    exit /b 1
)
echo Done: %BUILD_DIR%\relayFile-setup-%VERSION%-%COMMIT%.exe
endlocal
