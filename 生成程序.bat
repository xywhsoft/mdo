@echo off
setlocal
cd /d "%~dp0"
python tools\build_mdo.py --output mdo.exe
if errorlevel 1 exit /b 1
echo Built mdo.exe from the revisions locked in deps.lock.
