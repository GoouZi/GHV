@echo off
setlocal
cd /d "%~dp0"
if not exist bin mkdir bin
where cl >nul 2>nul
if %errorlevel%==0 (
  cl /nologo /O2 /EHsc /std:c++17 ghvcore.cpp /Fe:bin\ghvcore.exe
  if %errorlevel%==0 goto :ok
)
where g++ >nul 2>nul
if %errorlevel%==0 (
  g++ -O3 -std=c++17 -DNDEBUG ghvcore.cpp -o bin\ghvcore.exe
  if %errorlevel%==0 goto :ok
)
where clang++ >nul 2>nul
if %errorlevel%==0 (
  clang++ -O3 -std=c++17 -DNDEBUG ghvcore.cpp -o bin\ghvcore.exe
  if %errorlevel%==0 goto :ok
)
echo.
echo No C++ compiler found. Install Visual Studio Build Tools, MinGW-w64, or LLVM.
echo GHV Studio will still work using the NumPy fallback.
exit /b 1
:ok
echo.
echo Built native\bin\ghvcore.exe
