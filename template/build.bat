@echo off
setlocal enabledelayedexpansion
title Build Dream Mod DLL
cd /d "%~dp0"

set "PATH=C:\Temp\w64devkit\w64devkit\bin;%PATH%"

echo Compiling MyDreamMod.dll with g++ (C++20)...
g++ -std=c++20 -O2 -shared -o "MyDreamMod.dll" src/dllmain.cpp ../include/dreamkurumiHT/hde/hde64.c ../include/dreamkurumiHT/yyjson/yyjson.c -I../include -L../lib -llua51 -static-libgcc -static-libstdc++ -lpsapi -ldbghelp

if %errorlevel% equ 0 (
    echo [SUCCESS] MyDreamMod.dll compiled successfully!
) else (
    echo [ERROR] Compilation failed!
)
pause
