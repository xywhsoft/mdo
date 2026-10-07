@echo off
rem Reproducible build for the xge pixel editor (workspace-local output).
rem Requires the engine SDK at D:\GIT\xge (xge.h / lib\xrt headers, build\xge.lib + xge.dll).
setlocal
set XGE=D:\GIT\xge
set CC=gcc
set OUT=bin

if not exist %OUT% mkdir %OUT%

%CC% -O2 -std=c99 -Wall -Isrc -I%XGE% ^
  src\main.c src\editor.c src\app.c src\paths.c ^
  %XGE%\build\xge.lib -o %OUT%\pixel_editor.exe -lgdi32 -luser32 -lshell32 -lole32
if errorlevel 1 exit /b 1

copy /y %XGE%\build\xge.dll %OUT%\xge.dll >nul
rem runtime assets live next to the executable
copy /y res_font_KaTeX_Main-Regular.ttf %OUT%\ >nul
echo built: %OUT%\pixel_editor.exe
