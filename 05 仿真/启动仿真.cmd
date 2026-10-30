@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
    echo Node.js was not found. Install Node.js 20 or newer, then run this file again.
    pause
    exit /b 1
)
node "%~dp0server.mjs" %*
if errorlevel 1 pause
