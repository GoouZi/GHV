@echo off
cd /d "%~dp0"
if "%~1"=="" (
  echo Usage: play_audio.bat file.gha
  pause
  exit /b 2
)
python ghaplay.py "%~1"
