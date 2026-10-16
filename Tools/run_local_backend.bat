@echo off
rem Starts the local backend (Nakama + PostgreSQL in Docker Desktop) and waits until it answers.
rem Arguments are passed on, e.g.  Tools\run_local_backend.bat -Down     Tools\run_local_backend.bat -Logs
rem See docs\backend-setup.md.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_local_backend.ps1" %*
exit /b %ERRORLEVEL%
