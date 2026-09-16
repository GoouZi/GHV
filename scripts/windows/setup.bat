@echo off
setlocal
cd /d "%~dp0..\.."
echo [GHV 0.9 Beta] Installing Python runtime dependencies...
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
echo       Conversion needs ffmpeg/ffprobe. The native Windows player does not.
goto :native
:ffok
echo [GHV] FFmpeg detected.
:native
if exist "native\bin\ghvcore.exe" if exist "native\bin\ghvdecode.exe" (
  echo [GHV] Native GHVC6-GHVC9 encoder and decoder already built.
  goto :done
)
where cl >nul 2>nul
if %errorlevel%==0 goto :build
where g++ >nul 2>nul
if %errorlevel%==0 goto :build
where clang++ >nul 2>nul
if %errorlevel%==0 goto :build
echo [GHV] No C++ compiler detected. Some legacy Python compatibility paths remain available.
echo       For GHVC9 encoding/decoding, install Visual Studio Build Tools,
echo       MinGW-w64, or LLVM, then run native\build_windows.bat.
goto :done
:build
echo [GHV] C++ compiler detected; building native GHVC6-GHVC9 encoder + decoder...
call native\build_windows.bat
:done
echo.
echo [GHV] Setup complete. Run scripts\windows\studio_ghv.bat or studio_gha.bat.
pause
