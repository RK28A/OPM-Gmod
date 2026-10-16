@echo off
setlocal
cd /d "%~dp0"
title LagControl

set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY (
  echo Python 3.10 ou plus recent est introuvable.
  echo Installez-le depuis https://www.python.org/downloads/
  echo et cochez "Add python.exe to PATH" pendant l'installation.
  pause
  exit /b 1
)

%PY% -c "import customtkinter, psutil, pydivert, keyboard" >nul 2>nul
if errorlevel 1 (
  echo Installation des dependances, patientez...
  %PY% -m pip install --upgrade -r requirements.txt
  if errorlevel 1 (
    echo.
    echo Echec de l'installation des dependances.
    pause
    exit /b 1
  )
)

rem L'application demande elle-meme les droits administrateur (fenetre UAC).
%PY% main.py
if errorlevel 1 pause
