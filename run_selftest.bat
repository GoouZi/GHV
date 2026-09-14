@echo off
cd /d "%~dp0"
python tests\selftest_v06.py
if errorlevel 1 exit /b 1
python tests\selftest_v07.py
pause
