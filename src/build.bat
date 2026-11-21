@echo off
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
for /f "usebackq delims=" %%i in (`call "%%VSWHERE%%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio bulunamadi & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64 bulunamadi & exit /b 1)
if not exist JustBetterTaskBar.ico powershell -NoProfile -ExecutionPolicy Bypass -File make_icon.ps1
rc /nologo /fo JustBetterTaskBar.res JustBetterTaskBar.rc || exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++20 /utf-8 /W3 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS JustBetterTaskBar.cpp JustBetterTaskBar.res /Fe:JustBetterTaskBar.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED
if errorlevel 1 exit /b 1
del /q JustBetterTaskBar.obj JustBetterTaskBar.res 2>nul
echo.
echo Derleme tamam: %~dp0JustBetterTaskBar.exe
