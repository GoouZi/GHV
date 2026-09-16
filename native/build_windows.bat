@echo off
setlocal
cd /d "%~dp0"
python "%~dp0..\tools\generate_version_header.py"
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
where cl >nul 2>nul
if %errorlevel%==0 (
  cl /nologo /O2 /EHsc /std:c++17 /openmp ghvcore.cpp /Fe:bin\ghvcore.exe
  if errorlevel 1 cl /nologo /O2 /EHsc /std:c++17 ghvcore.cpp /Fe:bin\ghvcore.exe
  if errorlevel 1 goto :try_gpp
  cl /nologo /O2 /EHsc /std:c++17 /openmp ghvdecode.cpp /Fe:bin\ghvdecode.exe
  if errorlevel 1 cl /nologo /O2 /EHsc /std:c++17 ghvdecode.cpp /Fe:bin\ghvdecode.exe
  if not errorlevel 1 goto :ok
)
:try_gpp
where g++ >nul 2>nul
if %errorlevel%==0 (
  g++ -O3 -std=c++17 -DNDEBUG -fopenmp ghvcore.cpp -o bin\ghvcore.exe
  if errorlevel 1 g++ -O3 -std=c++17 -DNDEBUG ghvcore.cpp -o bin\ghvcore.exe
  if errorlevel 1 goto :try_clang
  g++ -O3 -std=c++17 -DNDEBUG -fopenmp ghvdecode.cpp -o bin\ghvdecode.exe
  if errorlevel 1 g++ -O3 -std=c++17 -DNDEBUG ghvdecode.cpp -o bin\ghvdecode.exe
  if not errorlevel 1 goto :ok
)
:try_clang
where clang++ >nul 2>nul
if %errorlevel%==0 (
  clang++ -O3 -std=c++17 -DNDEBUG ghvcore.cpp -o bin\ghvcore.exe
  if errorlevel 1 goto :fail
  clang++ -O3 -std=c++17 -DNDEBUG ghvdecode.cpp -o bin\ghvdecode.exe
  if not errorlevel 1 goto :ok
)
:fail
echo.
echo No working C++ compiler found. Install Visual Studio Build Tools, MinGW-w64, or LLVM.
echo GHV Studio can still run the NumPy fallback, but native is strongly recommended.
exit /b 1
:ok
echo.
echo Built native\bin\ghvcore.exe and ghvdecode.exe
