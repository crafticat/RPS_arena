@echo off
rem RPS Arena - build everything and open the UI (Windows)
rem Missing prerequisites (Python 3, C++ compiler) are offered as winget installs.
setlocal
cd /d "%~dp0"

rem ---- Python 3 ----
where py >nul 2>nul
if %errorlevel%==0 goto :have_python
where python >nul 2>nul
if %errorlevel%==0 goto :have_python

echo Python 3 was not found on this machine.
call :winget_install "Python 3" Python.Python.3.12 "--scope user"
if errorlevel 1 (
  echo.
  echo Install Python 3 manually from https://python.org
  echo ^(tick "Add python.exe to PATH" during setup^), then run this again.
  goto :done
)
call :refresh_path
where py >nul 2>nul
if %errorlevel%==0 goto :have_python
where python >nul 2>nul
if %errorlevel%==0 goto :have_python
echo Python installed, but this window doesn't see it yet - close this
echo window and run start.bat again.
goto :done

:have_python

rem ---- C++ compiler ----
where g++ >nul 2>nul
if %errorlevel%==0 goto :have_compiler
where cl >nul 2>nul
if %errorlevel%==0 goto :have_compiler
where clang++ >nul 2>nul
if %errorlevel%==0 goto :have_compiler

echo No C++ compiler ^(g++ / cl / clang++^) was found on this machine.
call :winget_install "MinGW-w64 g++ (WinLibs)" BrechtSanders.WinLibs.POSIX.UCRT ""
if errorlevel 1 (
  echo.
  echo Install a compiler manually: MinGW-w64 g++ from https://winlibs.com
  echo ^(unzip and add its bin\ folder to PATH^), then run this again.
  goto :done
)
call :refresh_path
where g++ >nul 2>nul
if %errorlevel%==0 goto :have_compiler
echo g++ installed, but this window doesn't see it yet - close this
echo window and run start.bat again.
goto :done

:have_compiler
where py >nul 2>nul
if %errorlevel%==0 (
  py -3 start.py %*
) else (
  python start.py %*
)
goto :done

rem ---- helpers ----

:winget_install
rem %1 = display name, %2 = winget package id, %3 = extra winget args
where winget >nul 2>nul
if errorlevel 1 (
  echo winget is not available on this machine, so it can't be installed automatically.
  exit /b 1
)
choice /c YN /m "Install %~1 now with winget"
if errorlevel 2 exit /b 1
winget install -e --id %~2 %~3 --accept-package-agreements --accept-source-agreements
exit /b %errorlevel%

:refresh_path
rem A just-installed tool isn't visible in this session's stale PATH -
rem re-read machine+user PATH from the registry, plus winget's links dir.
for /f "usebackq delims=" %%p in (`powershell -NoProfile -Command "[Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')"`) do set "PATH=%%p"
set "PATH=%PATH%;%LOCALAPPDATA%\Microsoft\WinGet\Links"
exit /b 0

:done
echo.
pause
