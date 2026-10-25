@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check Survivor 2 Files.ps1" -Folder "%~dp0roms"
if errorlevel 1 (
    pause
    exit /b 1
)
start "" /D "%~dp0" "%~dp0flycast.exe" "%~dp0roms\gunsur2.zip"
