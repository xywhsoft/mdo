@echo off
cd /d "%~dp0"
xsw.exe pack app -o mdo.exe
if errorlevel 1 (echo 打包失败 & pause & exit /b 1)
echo.
echo 完成：mdo.exe（单文件便携版）
echo 数据将保存在 mdo.exe 同目录的 data\ 下；整目录拷贝即完成备份/迁移。
pause
