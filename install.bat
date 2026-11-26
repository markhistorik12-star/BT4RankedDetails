@echo off
setlocal
set "SRC=%~dp0"
set "DST=%~1"
if "%DST%"=="" set "DST=%~dp0..\BT4decomp"

if not exist "%DST%\BT4decomp.exe" (
  echo [ERROR] BT4decomp.exe not found in: %DST%
  echo Usage: install.bat "C:\path\to\BT4decomp"
  exit /b 1
)
if not exist "%SRC%build\winmm.dll" (
  echo [ERROR] build\winmm.dll not found. Run build.bat first.
  exit /b 1
)

echo Installing BT4 Ranked Details into %DST% ...
copy /Y "%SRC%build\winmm.dll" "%DST%\winmm.dll" >nul
if not exist "%DST%\mods\bt4_ranked_details" mkdir "%DST%\mods\bt4_ranked_details"
copy /Y "%SRC%mod_lua\bt4_ranked_details\mod.lua" "%DST%\mods\bt4_ranked_details\mod.lua" >nul

if not exist "%DST%\ranked_details\history" mkdir "%DST%\ranked_details\history"
if not exist "%DST%\ranked_details\history\vendor" mkdir "%DST%\ranked_details\history\vendor"
if not exist "%DST%\ranked_details\history\chars" mkdir "%DST%\ranked_details\history\chars"
if not exist "%DST%\ranked_details\data" mkdir "%DST%\ranked_details\data"

copy /Y "%SRC%history\index.html" "%DST%\ranked_details\history\index.html" >nul
copy /Y "%SRC%history\app.js" "%DST%\ranked_details\history\app.js" >nul
copy /Y "%SRC%history\app.css" "%DST%\ranked_details\history\app.css" >nul
copy /Y "%SRC%history\build-history.py" "%DST%\ranked_details\history\build-history.py" >nul
copy /Y "%SRC%history\refresh.bat" "%DST%\ranked_details\history\refresh.bat" >nul
if exist "%SRC%history\vendor\*" copy /Y "%SRC%history\vendor\*" "%DST%\ranked_details\history\vendor\" >nul
if exist "%SRC%history\chars\*.webp" copy /Y "%SRC%history\chars\*.webp" "%DST%\ranked_details\history\chars\" >nul
if exist "%SRC%data\characters.tsv" copy /Y "%SRC%data\characters.tsv" "%DST%\ranked_details\data\characters.tsv" >nul
if not exist "%DST%\ranked_details\BT4RankedDetails.ini" (
  if exist "%SRC%BT4RankedDetails.ini" copy /Y "%SRC%BT4RankedDetails.ini" "%DST%\ranked_details\BT4RankedDetails.ini" >nul
)
if not exist "%DST%\ranked_details\history\data.js" (
  if exist "%SRC%history\data.js" copy /Y "%SRC%history\data.js" "%DST%\ranked_details\history\data.js" >nul
)

echo.
echo [OK] BT4 Ranked Details installed!
echo  - Proxy DLL:  %DST%\winmm.dll (BT4decomp.exe untouched)
echo  - Lua Mod:    %DST%\mods\bt4_ranked_details\mod.lua (enable in F1 ^> Mods)
echo  - History UI: %DST%\ranked_details\history\index.html
endlocal
