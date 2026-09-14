@echo off
cd /d "%~dp0"
if "%~1"=="" (
  echo Usage: encode_audio.bat input_audio [output.gha]
  pause
  exit /b 2
)
set "OUT=%~2"
if "%OUT%"=="" set "OUT=%~dpn1.gha"
python ghaenc.py "%~1" "%OUT%" --mode hq
pause
