@echo off
rem gomd build script for Windows (double-click to run)
rem NOTE: -H windowsgui is the Go equivalent of gcc's -mwindows:
rem       it sets the PE subsystem to GUI so no console window appears.
setlocal
cd /d "%~dp0"

set WINDRES=windres
where windres >nul 2>nul
if errorlevel 1 set WINDRES=D:\msys64\ucrt64\bin\windres.exe

pushd resources
"%WINDRES%" gomd.rc -O coff -o ..\src\gomd.syso
popd

go build -trimpath -ldflags="-s -w -H windowsgui" -o dist\gomd.exe .\src
if errorlevel 1 goto :fail

copy /Y docs\sample.md dist\ >nul
echo Build OK: dist\gomd.exe  (GUI subsystem, no console window)
goto :eof

:fail
echo Build FAILED
exit /b 1
