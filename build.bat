@echo off
setlocal
cd /d "%~dp0"

set "VCVARS="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if "%VCVARS%"=="" if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if "%VCVARS%"=="" if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if "%VCVARS%"=="" (
  echo [ERROR] Could not find vcvars64.bat.
  exit /b 1
)

call "%VCVARS%" >nul
if not exist build mkdir build

echo [1/2] Building build\winmm.dll (x64 Release)...
cl /nologo /std:c++20 /O2 /EHsc /W3 /MD /DNOMINMAX /DUNICODE /D_UNICODE /LD ^
   src\winmm_proxy.cpp ^
   /Fe:build\winmm.dll /Fo:build\ ^
   /link /DEF:src\winmm.def user32.lib gdi32.lib gdiplus.lib winhttp.lib
if errorlevel 1 (
  echo [ERROR] Build of winmm.dll failed.
  exit /b 1
)

echo [2/2] Building build\test_telemetry.exe...
cl /nologo /std:c++20 /O2 /EHsc /W3 /MD /DNOMINMAX /DUNICODE /D_UNICODE ^
   tests\test_telemetry.cpp ^
   /Fe:build\test_telemetry.exe /Fo:build\ ^
   /link user32.lib gdi32.lib winhttp.lib
if errorlevel 1 (
  echo [ERROR] Build of test_telemetry.exe failed.
  exit /b 1
)

echo [OK] Build succeeded: build\winmm.dll and build\test_telemetry.exe
endlocal
