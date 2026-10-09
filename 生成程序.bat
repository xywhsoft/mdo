@echo off
setlocal
cd /d "%~dp0"
python tools\build.py %*
if errorlevel 1 exit /b 1
echo Built mdo.exe, mdo-arm64-v8a.apk and mdo-full-arm64-v8a.apk.
