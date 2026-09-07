@echo off
REM ============================================================
REM  One-click flash for STM32F103C8T6 gamepad
REM  J-Link V8 clone -> MUST use V4.15e driver (NEVER upgrade fw)
REM  Usage: double-click this file. J-Link connected, board powered.
REM ============================================================
setlocal
cd /d "%~dp0"

set JLINK="C:\Program Files (x86)\SEGGER\JLinkARM_V415e\JLink.exe"
set HEX=Objects\demo.hex

if not exist "%JLINK%" (
    echo [ERROR] JLink.exe not found. Install JLinkARM_V415e first.
    pause
    exit /b 1
)

if not exist "%HEX%" (
    echo [ERROR] %HEX% not found. Build the project first.
    pause
    exit /b 1
)

echo ============================================================
echo  Flashing: %HEX%
echo  Target: STM32F103C8T6   Interface: SWD   Speed: 4MHz
echo ============================================================
echo.
%JLINK% -device STM32F103C8 -if SWD -speed 4000 -CommanderScript flash.jlink
echo.
echo ============================================================
echo  Done. If no error above, flash succeeded.
echo ============================================================
pause
endlocal
