@echo off
setlocal
cd /d "%~dp0"

echo ========================================================
echo  Building MyDreamModCSharp with .NET 8 / .NET 10
echo ========================================================

dotnet build MyDreamModCSharp.csproj -c Release

if %errorlevel% equ 0 (
    echo.
    echo [SUCCESS] MyDreamModCSharp compiled successfully!
) else (
    echo.
    echo [ERROR] Compilation failed!
)
pause
