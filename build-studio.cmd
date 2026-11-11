@echo off
rem Builds Folio Studio. Needs Git for Windows, whose bash runs the script.
setlocal
set "BASH="
for /f "delims=" %%G in ('where git 2^>nul') do if not defined BASH if exist "%%~dpG..\bin\bash.exe" set "BASH=%%~dpG..\bin\bash.exe"
if not defined BASH if exist "%ProgramFiles%\Git\bin\bash.exe" set "BASH=%ProgramFiles%\Git\bin\bash.exe"
if not defined BASH (
	echo Git for Windows is not installed: https://git-scm.com/download/win
	exit /b 1
)
"%BASH%" "%~dp0scripts/studio.sh" build
exit /b %ERRORLEVEL%
