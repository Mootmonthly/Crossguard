@echo off
rem Editor set-up without clicking: data tables, DA_SkillCatalog, game mode and character Blueprint
rem references (Tools\editor_setup.py in UnrealEditor-Cmd). Builds first. Arguments are passed on,
rem e.g.  Tools\editor_setup.bat -SkipBuild -CreateMap
rem Report: Saved\EditorSetup\summary.txt. See docs\pc-session.md.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0editor_setup.ps1" %*
exit /b %ERRORLEVEL%
