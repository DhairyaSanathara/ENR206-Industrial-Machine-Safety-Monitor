@echo off
title Machine Safety Monitor (demo)
cd /d "%~dp0"
set PY=python
%PY% --version >nul 2>&1 || set PY=py
%PY% --version >nul 2>&1 || (
  echo Python is not installed. Download it from https://www.python.org/downloads/
  echo and tick "Add python.exe to PATH" during setup, then run this file again.
  pause
  exit /b
)
%PY% -m pip install --quiet --disable-pip-version-check pyserial
%PY% server.py --demo %*
pause
