@echo off
rem Optional: rebuild data.js from ..\data\ (the mod already does this after every match) and open the page.
setlocal
cd /d "%~dp0"
where python >nul 2>nul
if errorlevel 1 (
  echo Python not found - that is fine, the mod keeps data.js up to date by itself. Opening the page.
) else (
  python build-history.py
  if errorlevel 1 (
    echo build-history.py failed.
    pause
    exit /b 1
  )
)
start "" "%~dp0index.html"
