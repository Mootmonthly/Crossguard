@echo off
rem Starts a dedicated hub server, a dedicated match server and N clients on this PC. Arguments are
rem passed on, e.g.  Tools\run_local_cluster.bat -Clients 3     Tools\run_local_cluster.bat -Stop
rem Logs: Saved\LocalCluster\. See docs\dedicated-server-plan.md.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_local_cluster.ps1" %*
exit /b %ERRORLEVEL%
