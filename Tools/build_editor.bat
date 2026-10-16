@echo off
rem Builds CrossguardEditor and prints the errors compactly (file:line: error ...). Arguments are
rem passed on, e.g.  Tools\build_editor.bat -MaxErrors 100 -KillEditor
rem Log: Saved\Build\build.log, errors: Saved\Build\errors.txt. See docs\pc-session.md.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_editor.ps1" %*
exit /b %ERRORLEVEL%
