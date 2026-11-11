@echo off
rem Builds Folio Studio. Needs Git for Windows, whose bash runs the script.
setlocal
set "BASH="
set "RESULT=1"
for /f "delims=" %%G in ('where git 2^>nul') do if not defined BASH if exist "%%~dpG..\bin\bash.exe" set "BASH=%%~dpG..\bin\bash.exe"
if not defined BASH if exist "%ProgramFiles%\Git\bin\bash.exe" set "BASH=%ProgramFiles%\Git\bin\bash.exe"
if not defined BASH (
	echo Git for Windows is not installed: https://git-scm.com/download/win
	goto done
)
"%BASH%" "%~dp0scripts/studio.sh" build
set "RESULT=%ERRORLEVEL%"
:done
rem Started from Explorer, the window would close before the error can be read.
if not "%RESULT%"=="0" echo %CMDCMDLINE% | "%SystemRoot%\System32\find.exe" /i "%~nx0" >nul && pause
exit /b %RESULT%
