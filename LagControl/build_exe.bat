@echo off
setlocal
cd /d "%~dp0"

set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY (
  echo Python est introuvable.
  pause
  exit /b 1
)

%PY% -m pip install --upgrade -r requirements.txt pyinstaller || goto :error
%PY% -m PyInstaller --noconfirm --clean --onefile --windowed --uac-admin ^
  --name LagControl --collect-all customtkinter --collect-all pydivert main.py || goto :error

echo.
echo Termine : dist\LagControl.exe
pause
exit /b 0

:error
echo.
echo La compilation a echoue.
pause
exit /b 1
