@echo off
rem Builds CrossguardEditor and runs the Crossguard editor tests headlessly. Arguments are passed on,
rem e.g.  Tools\run_editor_tests.bat -Filter Crossguard.Loot -SkipBuild
rem Report: Saved\EditorTests\summary.txt. See docs\editor-tests.md.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_editor_tests.ps1" %*
exit /b %ERRORLEVEL%
