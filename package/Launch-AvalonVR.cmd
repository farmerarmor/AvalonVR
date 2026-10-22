@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-AvalonVR.ps1" %*
if errorlevel 1 pause
