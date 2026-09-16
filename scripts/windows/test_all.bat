@echo off
cd /d "%~dp0..\.."
python tests\test_version_sync.py
if errorlevel 1 exit /b 1
python tests\selftest_v04.py
if errorlevel 1 exit /b 1
python tests\selftest_v05.py
if errorlevel 1 exit /b 1
python tests\selftest_v06.py
if errorlevel 1 exit /b 1
python tests\selftest_v07.py
if errorlevel 1 exit /b 1
python tests\selftest_v08.py
if errorlevel 1 exit /b 1
python tests\selftest_v09.py
if errorlevel 1 exit /b 1
echo [PASS] All GHV compatibility self-tests completed.
