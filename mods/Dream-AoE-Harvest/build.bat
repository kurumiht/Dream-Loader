@echo off
setlocal enabledelayedexpansion
title Build DreamAoEHarvest.dll (Harmony Plugin - Unified Client and Server)
cd /d "%~dp0"

:: Auto-detect GCC / MinGW toolchain
where g++ >nul 2>&1
if %errorlevel% neq 0 (
    if exist "C:\Temp\w64devkit\w64devkit\bin\g++.exe" (
        set "PATH=C:\Temp\w64devkit\w64devkit\bin;!PATH!"
    ) else if exist "C:\w64devkit\bin\g++.exe" (
        set "PATH=C:\w64devkit\bin;!PATH!"
    ) else if exist "C:\msys64\ucrt64\bin\g++.exe" (
        set "PATH=C:\msys64\ucrt64\bin;!PATH!"
    ) else if exist "C:\msys64\mingw64\bin\g++.exe" (
        set "PATH=C:\msys64\mingw64\bin;!PATH!"
    ) else if exist "%USERPROFILE%\w64devkit\bin\g++.exe" (
        set "PATH=%USERPROFILE%\w64devkit\bin;!PATH!"
    ) else (
        echo [ERROR] g++ compiler not found in PATH or standard locations.
        echo Please install w64devkit or MSYS2 and ensure g++ is in your PATH.
        exit /b 1
    )
)

echo ========================================================
echo   Compiling DreamAoEHarvest.dll (Unified Client and Server)...
echo ========================================================

g++ -std=c++20 -O2 -s -ffunction-sections -fdata-sections -Wl,--gc-sections -shared -o "DreamAoEHarvest.dll" src/dllmain.cpp src/AoeHarvestAsm.s -I../../include -I../../dreamkurumiHT/include -static-libgcc -static-libstdc++

if %errorlevel% equ 0 (
    echo [SUCCESS] DreamAoEHarvest.dll compiled successfully!
    for %%I in ("DreamAoEHarvest.dll") do echo File Size: %%~zI bytes
) else (
    echo [ERROR] Compilation failed with error code %errorlevel%!
)
