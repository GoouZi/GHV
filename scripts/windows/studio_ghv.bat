@echo off
cd /d "%~dp0..\.."
python -m apps.ghv_studio
if errorlevel 1 pause
