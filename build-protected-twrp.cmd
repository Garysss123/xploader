@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>&1
if errorlevel 1 (
  python tools\make_protected_twrp.py
) else (
  py -3 tools\make_protected_twrp.py
)
set xpl_result=%errorlevel%
if not "%xpl_result%"=="0" echo Build failed. Check the error above and confirm Python 3 is installed.
pause
exit /b %xpl_result%
