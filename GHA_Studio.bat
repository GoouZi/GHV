@echo off
cd /d "%~dp0"
python gha_gui.py
if errorlevel 1 pause
