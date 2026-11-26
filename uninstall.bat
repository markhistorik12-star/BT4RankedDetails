@echo off
setlocal
set "DST=%~1"
if "%DST%"=="" set "DST=%~dp0..\BT4decomp"

if exist "%DST%\winmm.dll" (
  del /F /Q "%DST%\winmm.dll"
  echo Removed %DST%\winmm.dll
) else (
  echo winmm.dll not found in %DST%
)
echo Note: Your match history in %DST%\ranked_details\data\ was preserved.
endlocal
