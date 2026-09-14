@echo off
cd /d "%~dp0"
if "%~1"=="" (echo Drag a .ghv file onto this BAT or run: repair.bat video.ghv & pause & exit /b 1)
python ghvrepair.py "%~1"
pause
