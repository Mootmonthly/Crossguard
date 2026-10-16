@echo off
rem Builds the CrossguardServer target (dedicated server) with a SOURCE build of UE 5.8. Arguments are
rem passed on, e.g.  Tools\build_server.bat -Package -Platform Linux -EngineDir D:\UE\UE_5.8_Source
rem Logs: Saved\ServerBuild\. See docs\dedicated-server-plan.md.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_server.ps1" %*
exit /b %ERRORLEVEL%
