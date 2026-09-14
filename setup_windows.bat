@echo off
setlocal
cd /d "%~dp0"
echo [GHV 0.5] Installing Python runtime dependencies...
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
echo [GHV] WARNING: FFmpeg/ffplay was not found in PATH, E:\ffmpeg\bin, or C:\ffmpeg\bin.
echo       Conversion needs ffmpeg. Native playback presentation also uses ffplay.
goto :native
:ffok
echo [GHV] FFmpeg detected.
:native
if exist "native\bin\ghvcore.exe" if exist "native\bin\ghvdecode.exe" (
  echo [GHV] Native GHVC4 encoder and decoder already built.
  goto :done
)
where cl >nul 2>nul
if %errorlevel%==0 goto :build
where g++ >nul 2>nul
if %errorlevel%==0 goto :build
where clang++ >nul 2>nul
if %errorlevel%==0 goto :build
echo [GHV] No C++ compiler detected. Python/NumPy compatibility mode will be used.
echo       For much faster encoding and more stable playback, install Visual Studio Build Tools,
echo       MinGW-w64, or LLVM, then run build_native_windows.bat.
goto :done
:build
echo [GHV] C++ compiler detected; building native GHVC4 encoder + decoder...
call native\build_windows.bat
:done
echo.
echo [GHV] Setup complete. Double-click GHV_Studio.bat or GHA_Studio.bat.
pause
