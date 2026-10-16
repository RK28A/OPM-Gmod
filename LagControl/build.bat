@echo off
rem ==========================================================================
rem  LagControl - compilation en un double-clic
rem
rem  1. trouve Visual Studio / Build Tools (propose de les installer sinon)
rem  2. telecharge WinDivert 2.2.2 (une seule fois, empreinte SHA-256 verifiee)
rem  3. compile et place le resultat dans dist\
rem       dist\LagControl.exe + WinDivert.dll + WinDivert64.sys
rem ==========================================================================
setlocal EnableExtensions
cd /d "%~dp0"
title Compilation de LagControl

set "WD_VER=2.2.2"
set "WD_DIR=third_party\WinDivert-%WD_VER%-A"
set "WD_ZIP=third_party\WinDivert-%WD_VER%-A.zip"
set "WD_URL=https://github.com/basil00/WinDivert/releases/download/v%WD_VER%/WinDivert-%WD_VER%-A.zip"
set "WD_SHA=63cb41763bb4b20f600b6de04e991a9c2be73279e317d4d82f237b150c5f3f15"

echo.
echo  ==== LagControl : compilation ====
echo.

rem ---------------------------------------------------------- 1. compilateur
where cl >nul 2>nul && goto :have_compiler

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vs
set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :no_vs
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto :no_vs
echo [1/3] Compilateur : %VSPATH%
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl >nul 2>nul || goto :no_vs
goto :compiler_ready

:have_compiler
echo [1/3] Compilateur deja disponible dans ce terminal.
:compiler_ready

rem ---------------------------------------------------------- 2. WinDivert
if exist "%WD_DIR%\include\windivert.h" if exist "%WD_DIR%\x64\WinDivert.lib" goto :have_windivert
echo [2/3] Telechargement de WinDivert %WD_VER%...
if not exist third_party mkdir third_party
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop'; [Net.ServicePointManager]::SecurityProtocol='Tls12';" ^
  "Invoke-WebRequest -UseBasicParsing -Uri '%WD_URL%' -OutFile '%WD_ZIP%';" ^
  "$h=(Get-FileHash -Algorithm SHA256 '%WD_ZIP%').Hash.ToLower();" ^
  "if ($h -ne '%WD_SHA%') { Remove-Item '%WD_ZIP%'; throw ('Empreinte SHA-256 inattendue : ' + $h) }" ^
  "Expand-Archive -Force '%WD_ZIP%' 'third_party'; Remove-Item '%WD_ZIP%'"
if errorlevel 1 goto :download_failed
goto :windivert_ready

:have_windivert
echo [2/3] WinDivert %WD_VER% deja present.
:windivert_ready

rem ---------------------------------------------------------- 3. compilation
echo [3/3] Compilation (environ 30 secondes)...
if not exist build\obj mkdir build\obj
if not exist dist mkdir dist

cl /nologo /MP /std:c++17 /utf-8 /O2 /EHsc /MT /W3 /DNDEBUG ^
   /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /DIMGUI_DISABLE_DEMO_WINDOWS ^
   /I third_party\imgui /I "%WD_DIR%\include" ^
   src\*.cpp third_party\imgui\imgui*.cpp ^
   third_party\imgui\backends\imgui_impl_win32.cpp third_party\imgui\backends\imgui_impl_dx11.cpp ^
   /Fo:build\obj\ /Fe:dist\LagControl.exe ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'" ^
   /LIBPATH:"%WD_DIR%\x64" WinDivert.lib d3d11.lib dxgi.lib d3dcompiler.lib iphlpapi.lib ws2_32.lib ^
   winmm.lib comdlg32.lib shell32.lib ole32.lib user32.lib gdi32.lib
if errorlevel 1 goto :build_failed

copy /y "%WD_DIR%\x64\WinDivert.dll" dist\ >nul || goto :build_failed
copy /y "%WD_DIR%\x64\WinDivert64.sys" dist\ >nul || goto :build_failed

echo.
echo  ==== Termine ! ====
echo.
echo  Le programme est dans le dossier  dist\
echo     LagControl.exe  +  WinDivert.dll  +  WinDivert64.sys
echo  Gardez ces 3 fichiers ensemble, puis lancez LagControl.exe.
echo.
if not defined CI start "" explorer "%~dp0dist"
goto :end

rem ---------------------------------------------------------- erreurs
:no_vs
echo.
echo  Visual Studio (ou les "Build Tools") avec le C++ n'est pas installe.
echo.
if defined CI goto :fail
where winget >nul 2>nul || goto :no_winget
choice /c ON /m " Voulez-vous l'installer automatiquement maintenant (environ 2 Go) ? O = oui, N = non"
if errorlevel 2 goto :no_winget
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-package-agreements --accept-source-agreements ^
  --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
echo.
echo  Installation terminee. Relancez build.bat.
goto :end

:no_winget
echo  Installez "Build Tools for Visual Studio 2022" depuis :
echo     https://visualstudio.microsoft.com/fr/downloads/
echo  et cochez "Developpement Desktop en C++", puis relancez build.bat.
goto :fail

:download_failed
echo.
echo  Le telechargement de WinDivert a echoue. Verifiez votre connexion Internet,
echo  ou telechargez %WD_URL%
echo  et dezippez-le dans le dossier third_party\ puis relancez build.bat.
goto :fail

:build_failed
echo.
echo  La compilation a echoue (voir les messages ci-dessus).
goto :fail

:fail
if not defined CI pause
exit /b 1

:end
if not defined CI pause
exit /b 0
