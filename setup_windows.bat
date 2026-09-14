@echo off
setlocal
cd /d "%~dp0"
echo [GHV] Installing Python runtime dependencies...
python -m pip install -r requirements.txt
if errorlevel 1 (
  echo.
  echo Setup failed. Make sure Python 3.10+ is installed and available as "python".
  pause
  exit /b 1
)
echo.
where ffmpeg >nul 2>nul
if %errorlevel%==0 goto :ffok
if exist "E:\ffmpeg\bin\ffmpeg.exe" goto :ffok
if exist "C:\ffmpeg\bin\ffmpeg.exe" goto :ffok
echo [GHV] WARNING: FFmpeg was not found in PATH, E:\ffmpeg\bin, or C:\ffmpeg\bin.
echo             Install FFmpeg before converting media.
goto :native
:ffok
echo [GHV] FFmpeg detected.
:native
if exist "native\bin\ghvcore.exe" (
  echo [GHV] Native GHVC3 core already built.
  goto :done
)
where cl >nul 2>nul
if %errorlevel%==0 goto :build
where g++ >nul 2>nul
if %errorlevel%==0 goto :build
where clang++ >nul 2>nul
if %errorlevel%==0 goto :build
echo [GHV] No C++ compiler detected. NumPy fallback will be used.
echo       For faster long-video encoding, install Visual Studio Build Tools, MinGW-w64, or LLVM, then run build_native_windows.bat.
goto :done
:build
echo [GHV] C++ compiler detected; building native GHVC3 core...
call native\build_windows.bat
:done
echo.
echo [GHV] Setup complete. Double-click GHV_Studio.bat or GHA_Studio.bat.
pause
