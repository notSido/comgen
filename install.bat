@echo off
setlocal

echo Installing comgen for Windows...

:: Check if comgen.exe exists
if not exist comgen.exe (
    echo comgen.exe not found. Attempting to build...
    where gcc >nul 2>nul
    if %errorlevel% neq 0 (
        echo Error: gcc not found. Please install MinGW or download comgen.exe.
        pause
        exit /b 1
    )
    
    echo Compiling...
    gcc -Wall -Wextra -O2 -o comgen.exe comgen.c -lwinhttp -luser32 -lkernel32 -static
    if %errorlevel% neq 0 (
        echo Build failed.
        pause
        exit /b 1
    )
)

:: Install
echo Installing to %SystemRoot%...
copy /Y comgen.exe "%SystemRoot%\comgen.exe"

if %errorlevel% neq 0 (
    echo.
    echo Installation failed. Please run this script as Administrator.
    pause
    exit /b 1
)

echo.
echo Done! You can now run 'comgen' from anywhere.
echo Run 'comgen' to complete configuration.
pause
