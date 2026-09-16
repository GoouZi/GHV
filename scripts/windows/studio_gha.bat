@echo off
cd /d "%~dp0..\.."
python -m apps.gha_studio
if errorlevel 1 pause
