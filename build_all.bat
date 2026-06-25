@echo off
title Neverlose.cc - Build All Components

echo ========================================
echo  Neverlose.cc - Build All Components
echo ========================================
echo.

:: Check for Visual Studio
where msbuild >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] Visual Studio not found or msbuild not in PATH
    echo Please install Visual Studio 2026 with C++ support
    pause
    exit /b 1
)

:: Check for WDK
if not exist "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\km" (
    echo [WARNING] Windows Driver Kit (WDK) not found
    echo Driver compilation may fail
)

:: Clean previous builds
echo [INFO] Cleaning previous builds...
if exist outputs rmdir /s /q outputs
mkdir outputs
if exist build rmdir /s /q build
mkdir build

:: Build Cheat DLL
echo.
echo [INFO] Building Cheat DLL...
msbuild cheat\neverlose_cheat.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=./ /m
if %errorlevel% neq 0 (
    echo [ERROR] Cheat build failed!
    pause
    exit /b 1
)

:: Build Loader
echo.
echo [INFO] Building Loader...
msbuild loader\neverlose_loader.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=./ /m
if %errorlevel% neq 0 (
    echo [ERROR] Loader build failed!
    pause
    exit /b 1
)

:: Build Driver (if WDK installed)
if exist "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\km" (
    echo.
    echo [INFO] Building Driver...
    msbuild driver\neverlose_driver.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=./ /m
    if %errorlevel% neq 0 (
        echo [WARNING] Driver build failed - WDK may not be properly configured
    )
) else (
    echo.
    echo [INFO] WDK not found - skipping driver build
)

echo.
echo ========================================
echo  BUILD COMPLETE!
echo ========================================
echo.
echo Outputs in: outputs\
echo   neverlose.dll     - Main cheat
echo   neverlose_loader.exe - Loader
echo   neverlose.sys     - Driver (if built)
echo.
echo To deploy:
echo   1. Run as Administrator
echo   2. neverlose_loader.exe
echo.
pause
