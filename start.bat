@echo off
rem RPS Arena - build everything and open the UI (Windows)
setlocal
cd /d "%~dp0"

where py >nul 2>nul
if %errorlevel%==0 (
  py -3 start.py %*
  goto :done
)
where python >nul 2>nul
if %errorlevel%==0 (
  python start.py %*
  goto :done
)
echo Python 3 is required. Install it from https://python.org
echo and tick "Add python.exe to PATH" during setup, then run this again.

:done
echo.
pause
