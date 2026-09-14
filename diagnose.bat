@echo off
cd /d "%~dp0"
if "%~1"=="" (
  echo Drag a .ghv file onto diagnose.bat, or run: diagnose.bat file.ghv
  pause
  exit /b 1
)
python ghvdoctor.py "%~1"
pause
