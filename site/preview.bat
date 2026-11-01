@echo off
cd /d "%~dp0"
if not exist node_modules (
	call npm ci || goto :fail
)
start "" /b cmd /c "timeout /t 3 /nobreak >nul & start http://localhost:4173/"
node build.mjs --serve
if errorlevel 1 goto :fail
exit /b 0
:fail
echo.
echo Preview failed. Is Node.js 20 or later installed?
pause
exit /b 1
