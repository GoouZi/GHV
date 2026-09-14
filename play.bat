@echo off
cd /d "%~dp0"
if "%~1"=="" (
  echo Usage: play.bat file.ghv
  pause
  exit /b 2
)
python ghvplay.py "%~1"
