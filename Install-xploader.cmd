@echo off
setlocal
title xploader v1.0 Installer
chcp 65001 >nul
cd /d "%~dp0"
set PYTHONUTF8=1
where py >nul 2>nul
if errorlevel 1 goto use_python
py -3 installer\app.py --apply
goto done
:use_python
where python >nul 2>nul
if errorlevel 1 goto missing_python
python installer\app.py --apply
goto done
:missing_python
echo Install Python 3 from https://www.python.org/downloads/windows/ first.
:done
echo.
pause
