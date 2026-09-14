@echo off
cd /d "%~dp0"
python ghvverify.py %*
if errorlevel 1 pause
