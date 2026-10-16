<#
.SYNOPSIS
    Does the checklist's editor set-up headlessly: imports / reimports the data tables, creates
    DA_SkillCatalog, sets the tables and catalog on BP_CrossguardGameMode and the character Blueprint.
    Runs Tools\editor_setup.py in UnrealEditor-Cmd (Python), no clicking.

.DESCRIPTION
    1. Makes sure Crossguard.uproject enables PythonScriptPlugin and EditorScriptingUtilities (adds
       them and says so if not).
    2. Builds CrossguardEditor (skip with -SkipBuild): the Python needs the compiled C++ classes.
    3. Runs UnrealEditor-Cmd.exe Crossguard.uproject -run=pythonscript -script=Tools\editor_setup.py
       (or, with -Mode Editor, -ExecutePythonScript=... in a full editor process that quits at the end).
    4. Prints Saved\EditorSetup\summary.txt (one PASS / FAIL / SKIP line per step).

    Exit codes: 0 every step passed (SKIP lines allowed: read them); 1 a step failed; 2 the build
    failed; 3 the editor wrote no summary (crash, Python error before the summary, timeout);
    4 bad set-up (engine or project not found).

    Close the editor first: it holds the assets open and a running editor blocks the build.

.PARAMETER EngineDir
    Engine root. Default: $env:UE_ENGINE_DIR, else the .uproject's EngineAssociation, else
    C:\Program Files\Epic Games\UE_5.8.

.PARAMETER SkipBuild
    Do not build first.

.PARAMETER CreateMap
    Also create /Game/Maps/Dev/L_DevSandbox (floor, light, player start, nav bounds, gather nodes,
    chest_common, mob spawner, dummy, stash chest, vendor NPC) if it does not exist.

.PARAMETER AddHubActors
    Add a stash chest and a vendor NPC to the existing L_DevSandbox if it has none (-CreateMap
    places them in a new map). Play the map with ?Hub to use them.

.PARAMETER CreateBlueprints
    Create BP_CrossguardGameMode / BP_CrossguardCharacter in /Game/Blueprints (native parents) when
    missing, instead of skipping them.

.PARAMETER KeepMappingContext
    Leave the character Blueprint's Default Mapping Context as it is (default: cleared, as the
    checklist asks, so the built-in key bindings apply).

.PARAMETER Mode
    Commandlet (default): -run=pythonscript. Editor: -ExecutePythonScript in a full (null RHI) editor
    process; try it if the commandlet cannot do a step (e.g. map creation).

.PARAMETER TimeoutMinutes
    Kill the editor after this long. Default 20.

.EXAMPLE
    Tools\editor_setup.bat
.EXAMPLE
    Tools\editor_setup.bat -SkipBuild -CreateMap
#>
[CmdletBinding()]
param(
    [string]$EngineDir = "",
    [switch]$SkipBuild,
    [switch]$CreateMap,
    [switch]$AddHubActors,
    [switch]$CreateBlueprints,
    [switch]$KeepMappingContext,
    [ValidateSet("Commandlet", "Editor")]
    [string]$Mode = "Commandlet",
    [int]$TimeoutMinutes = 20
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "ue_common.ps1")

$OutDir = Join-Path $CgRepoRoot "Saved\EditorSetup"
$SummaryFile = Join-Path $OutDir "summary.txt"
$EditorLog = Join-Path $OutDir "editor.log"
$EditorErrLog = Join-Path $OutDir "editor-stderr.log"
$BuildLog = Join-Path $OutDir "build.log"
$SetupScript = Join-Path $PSScriptRoot "editor_setup.py"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Show-Tail([string]$Path, [int]$Lines) {
    if (Test-Path $Path) {
        Write-Host "Last $Lines lines of $Path`:"
        foreach ($l in (Get-Content -Path $Path -Tail $Lines)) { Write-Host "  $l" }
    }
}

if (-not (Test-Path $CgProjectFile)) { Write-Host "SETUP ERROR: $CgProjectFile not found." -ForegroundColor Red; exit 4 }
try {
    $EngineRoot = Find-CgEngineDir $EngineDir
} catch {
    Write-Host ("SETUP ERROR: " + $_.Exception.Message) -ForegroundColor Red
    exit 4
}
Write-Host "Engine: $EngineRoot"
$EditorCmd = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

# --- Plugins ----------------------------------------------------------------------------------------
try {
    $added = Enable-CgProjectPlugins @("PythonScriptPlugin", "EditorScriptingUtilities")
    if ($added.Count -gt 0) {
        Write-Host ("Enabled in Crossguard.uproject: " + ($added -join ", ") + " (commit this change).") -ForegroundColor Yellow
    } else {
        Write-Host "Plugins: PythonScriptPlugin and EditorScriptingUtilities are enabled."
    }
} catch {
    Write-Host ("SETUP ERROR: plugin check: " + $_.Exception.Message) -ForegroundColor Red
    exit 4
}

if (Test-CgEditorRunning) {
    Write-Host "WARNING: an UnrealEditor process is running. Close the editor: it locks the assets this script saves and blocks the build." -ForegroundColor Yellow
}

# --- Build ------------------------------------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "Building CrossguardEditor Win64 Development (log: $BuildLog) ..."
    $result = Invoke-CgEditorBuild $EngineRoot $BuildLog
    if ($result.ExitCode -ne 0) {
        Write-Host ("BUILD FAILED (exit code {0}, {1} s). Log: {2}" -f $result.ExitCode, $result.Seconds, $BuildLog) -ForegroundColor Red
        if ($result.LiveCoding) { Write-Host "Hint: close the editor (Live Coding) and run again." -ForegroundColor Yellow }
        foreach ($e in ($result.Errors | Select-Object -First 40)) { Write-Host "  $e" }
        exit 2
    }
    Write-Host ("Build succeeded ({0} s)." -f $result.Seconds)
} else {
    Write-Host "Build skipped (-SkipBuild)."
}

if (-not (Test-Path $EditorCmd)) { Write-Host "SETUP ERROR: $EditorCmd not found." -ForegroundColor Red; exit 4 }

# --- Run the Python ---------------------------------------------------------------------------------
foreach ($stale in @($SummaryFile, $EditorLog, $EditorErrLog)) { Remove-Item -Path $stale -Force -ErrorAction SilentlyContinue }

# Options go through the environment (quoting arguments through -script= is fragile).
$env:CG_SETUP_CREATE_MAP = if ($CreateMap) { "1" } else { "" }
$env:CG_SETUP_ADD_HUB_ACTORS = if ($AddHubActors) { "1" } else { "" }
$env:CG_SETUP_CREATE_BLUEPRINTS = if ($CreateBlueprints) { "1" } else { "" }
$env:CG_SETUP_KEEP_MAPPING_CONTEXT = if ($KeepMappingContext) { "1" } else { "" }
$env:CG_SETUP_QUIT = if ($Mode -eq "Editor") { "1" } else { "" }

$common = @("-unattended", "-nullrhi", "-nosplash", "-nosound", "-NoP4", "-stdout", "-FullStdOutLogOutput", "-log")
if ($Mode -eq "Commandlet") {
    $editorArgs = @("`"$CgProjectFile`"", "-run=pythonscript", "-script=`"$SetupScript`"") + $common
} else {
    $editorArgs = @("`"$CgProjectFile`"", "-ExecutePythonScript=`"$SetupScript`"") + $common
}
$editorArgs = $editorArgs -join " "

Write-Host "Running editor_setup.py ($Mode, log: $EditorLog) ..."
Write-Host "  $EditorCmd $editorArgs"
$start = Get-Date
$proc = Start-Process -FilePath $EditorCmd -ArgumentList $editorArgs -NoNewWindow -PassThru `
    -RedirectStandardOutput $EditorLog -RedirectStandardError $EditorErrLog
$null = $proc.Handle
if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    try { $proc.Kill() } catch { }
    Write-Host "TIMEOUT: the editor ran longer than $TimeoutMinutes minutes and was stopped." -ForegroundColor Red
}
$proc.WaitForExit()
$seconds = [int]((Get-Date) - $start).TotalSeconds
Write-Host "Editor exit code $($proc.ExitCode) after $seconds s."

# --- Report -----------------------------------------------------------------------------------------
$logText = @(Get-Content -Path $EditorLog -ErrorAction SilentlyContinue)
$pyErrors = @($logText | Select-String -Pattern 'LogPython: Error', 'Traceback \(most recent call last\)', 'Fatal error', 'Assertion failed' |
    ForEach-Object { $_.Line.Trim() } | Select-Object -Unique -First 30)

if (-not (Test-Path $SummaryFile)) {
    Write-Host "NO SUMMARY: $SummaryFile was not written." -ForegroundColor Red
    if ($logText -match "Unknown commandlet|pythonscript.*not found|PythonScriptPlugin") {
        Write-Host "Is PythonScriptPlugin enabled and the editor rebuilt? (Crossguard.uproject Plugins)" -ForegroundColor Yellow
    }
    foreach ($e in $pyErrors) { Write-Host "  $e" -ForegroundColor Red }
    Show-Tail $EditorLog 60
    exit 3
}

Write-Host ""
$summary = @(Get-Content -Path $SummaryFile)
foreach ($line in $summary) {
    if ($line -match '^(FAIL|RESULT: FAIL)') { Write-Host $line -ForegroundColor Red }
    elseif ($line -match '^SKIP') { Write-Host $line -ForegroundColor Yellow }
    elseif ($line -match '^RESULT: PASS') { Write-Host $line -ForegroundColor Green }
    else { Write-Host $line }
}
Write-Host ""
Write-Host "Summary: $SummaryFile  |  Log: $EditorLog"

$resultLine = $summary | Where-Object { $_ -match '^RESULT: ' } | Select-Object -Last 1
if ($resultLine -and $resultLine -match '^RESULT: PASS') { exit 0 }
if ($pyErrors.Count -gt 0) {
    Write-Host "Python / editor errors in the log:"
    foreach ($e in $pyErrors) { Write-Host "  $e" -ForegroundColor Red }
}
exit 1
