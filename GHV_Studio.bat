@echo off
cd /d "%~dp0"
python ghv_gui.py
if errorlevel 1 pause
