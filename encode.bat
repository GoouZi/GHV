@echo off
cd /d "%~dp0"
if "%~1"=="" (
  echo Usage: encode.bat input_video [output.ghv]
  pause
  exit /b 2
)
set "OUT=%~2"
if "%OUT%"=="" set "OUT=%~dpn1.ghv"
python ghvenc.py "%~1" "%OUT%" --preset balanced
pause
