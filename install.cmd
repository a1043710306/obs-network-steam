@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
set "install_exit=%ERRORLEVEL%"
pause
exit /b %install_exit%
