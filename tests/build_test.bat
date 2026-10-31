@echo off
setlocal enabledelayedexpansion
title Build and Run dreamkurumiHT Verification Suite
cd /d "%~dp0"

set "PATH=C:\Temp\w64devkit\w64devkit\bin;%PATH%"

echo ========================================================
echo   Compiling dreamkurumiHT Test Suite (C++20)...
echo ========================================================

g++ -std=c++20 -O2 -I../include test_runner.cpp ../include/dreamkurumiHT/hde/hde64.c ../include/dreamkurumiHT/yyjson/yyjson.c -o test_dreamkurumi.exe -L../lib -llua51 -static-libgcc -static-libstdc++ -lpsapi -ldbghelp -luser32 -lgdi32 -lws2_32

if %errorlevel% neq 0 (
    echo [ERROR] Compilation failed!
    exit /b %errorlevel%
)

echo [SUCCESS] Compiled test_dreamkurumi.exe successfully!
echo.
echo ========================================================
echo   Verifying ABI v1 Struct Layout (Static Asserts)...
echo ========================================================
g++ -std=c++20 -I../include -I../include/dreamkurumiHT -I../abi ../abi/test_abi_layout.cpp -o test_abi_layout.exe
if %errorlevel% neq 0 (
    echo [ERROR] ABI layout verification failed!
    exit /b %errorlevel%
)
test_abi_layout.exe
del test_abi_layout.exe
echo.
echo Running Verification Test Suite...
echo.
test_dreamkurumi.exe

