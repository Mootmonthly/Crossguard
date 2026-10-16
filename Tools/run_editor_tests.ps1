<#
.SYNOPSIS
    Builds CrossguardEditor and runs the Crossguard editor automation tests headlessly, then prints a
    short report (also written to Saved\EditorTests\summary.txt).

.DESCRIPTION
    1. Builds CrossguardEditor Win64 Development with the engine's Build.bat (skip with -SkipBuild).
    2. Runs UnrealEditor-Cmd.exe with -nullrhi and "Automation RunTests <Filter>;Quit".
    3. Reads the exported report (Saved\EditorTests\index.json) and prints passed / failed tests with
       every error and warning message.

    Exit codes: 0 all tests passed; 1 a test failed or no test ran; 2 the build failed; 3 the editor
    produced no report (crash, timeout, bad engine path); 4 bad set-up (engine or project not found).

    See docs/editor-tests.md.

.PARAMETER EngineDir
    Engine root (the folder holding Engine\). Default: $env:UE_ENGINE_DIR, else the engine the
    .uproject's EngineAssociation points to (registry / Epic launcher), else
    C:\Program Files\Epic Games\UE_5.8.

.PARAMETER Filter
    Test name filter passed to "Automation RunTests" (prefix match; join several with '+').
    Default: Crossguard (every Crossguard test).

.PARAMETER SkipBuild
    Do not build first (use the binaries already built).

.PARAMETER TimeoutMinutes
    Kill the editor if the run takes longer. Default 30.

.PARAMETER Visual
    Rendering run: no -nullrhi, so the PIE windows draw (1280 x 720 each) and the visual scenarios
    (Crossguard.Visual.*, which only exist when the editor can render) take screenshots into
    Saved\EditorTests\Screenshots\<Test>\<Name>.png (cleared first). Default filter: Crossguard.Visual.
    The windows appear on the desktop while it runs; do not click into them.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Tools\run_editor_tests.ps1
.EXAMPLE
    Tools\run_editor_tests.bat -Filter Crossguard.Loot -SkipBuild
.EXAMPLE
    Tools\run_editor_tests.bat -Visual -SkipBuild
#>
[CmdletBinding()]
param(
    [string]$EngineDir = "",
    [string]$Filter = "Crossguard",
    [switch]$SkipBuild,
    [int]$TimeoutMinutes = 30,
    [switch]$Visual
)

if ($Visual -and -not $PSBoundParameters.ContainsKey("Filter")) { $Filter = "Crossguard.Visual" }

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ProjectFile = Join-Path $RepoRoot "Crossguard.uproject"
$ReportDir = Join-Path $RepoRoot "Saved\EditorTests"
$SummaryFile = Join-Path $ReportDir "summary.txt"
$BuildLog = Join-Path $ReportDir "build.log"
$BuildErrLog = Join-Path $ReportDir "build-stderr.log"
$EditorLog = Join-Path $ReportDir "editor.log"
$EditorErrLog = Join-Path $ReportDir "editor-stderr.log"
$IndexJson = Join-Path $ReportDir "index.json"
$ScreenshotDir = Join-Path $ReportDir "Screenshots"

$script:SummaryLines = New-Object System.Collections.Generic.List[string]

function Write-Summary([string]$Line, [string]$Color = "") {
    $script:SummaryLines.Add($Line)
    if ($Color) { Write-Host $Line -ForegroundColor $Color } else { Write-Host $Line }
}

function Save-Summary {
    New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
    [System.IO.File]::WriteAllLines($SummaryFile, $script:SummaryLines.ToArray(), (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ""
    Write-Host "Summary written to $SummaryFile"
}

function Exit-With([int]$Code) {
    Save-Summary
    exit $Code
}

# ---------------------------------------------------------------------------------------------------
# Engine location
# ---------------------------------------------------------------------------------------------------

function Get-EngineAssociation {
    try {
        $project = [System.IO.File]::ReadAllText($ProjectFile) | ConvertFrom-Json
        return [string]$project.EngineAssociation
    } catch {
        return ""
    }
}

# Normalizes a folder to the engine root (the folder that contains Engine\). "" if it is not one.
function Resolve-EngineRoot([string]$Dir) {
    if (-not $Dir) { return "" }
    $Dir = $Dir.Trim().Trim('"').TrimEnd('\', '/')
    if (Test-Path (Join-Path $Dir "Engine\Build\BatchFiles\Build.bat")) { return $Dir }
    if ((Split-Path $Dir -Leaf) -eq "Engine" -and (Test-Path (Join-Path $Dir "Build\BatchFiles\Build.bat"))) {
        return (Split-Path $Dir -Parent)
    }
    return ""
}

function Find-EngineDir {
    $tried = New-Object System.Collections.Generic.List[string]

    if ($EngineDir) {
        $root = Resolve-EngineRoot $EngineDir
        if ($root) { return $root }
        throw "-EngineDir '$EngineDir' is not an engine folder (no Engine\Build\BatchFiles\Build.bat)."
    }
    if ($env:UE_ENGINE_DIR) {
        $root = Resolve-EngineRoot $env:UE_ENGINE_DIR
        if ($root) { return $root }
        $tried.Add("UE_ENGINE_DIR=$($env:UE_ENGINE_DIR)")
    }

    $association = Get-EngineAssociation
    if (-not $association) { $association = "5.8" }

    # Source builds registered by UnrealVersionSelector: HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds, value name = association.
    try {
        $builds = Get-ItemProperty -Path "HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds" -ErrorAction Stop
        $value = $builds.PSObject.Properties | Where-Object { $_.Name -eq $association } | Select-Object -First 1
        if ($value) {
            $root = Resolve-EngineRoot ([string]$value.Value)
            if ($root) { return $root }
            $tried.Add("HKCU Builds\$association=$($value.Value)")
        }
    } catch { }

    # Launcher installs: HKLM\SOFTWARE\EpicGames\Unreal Engine\<version>\InstalledDirectory.
    foreach ($key in @("HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association", "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$association")) {
        try {
            $installed = (Get-ItemProperty -Path $key -ErrorAction Stop).InstalledDirectory
            $root = Resolve-EngineRoot $installed
            if ($root) { return $root }
            $tried.Add("$key=$installed")
        } catch { }
    }

    # The Epic launcher's own list.
    $launcherDat = Join-Path $env:ProgramData "Epic\UnrealEngineLauncher\LauncherInstalled.dat"
    if (Test-Path $launcherDat) {
        try {
            $list = [System.IO.File]::ReadAllText($launcherDat) | ConvertFrom-Json
            foreach ($entry in $list.InstallationList) {
                if ($entry.AppName -eq "UE_$association") {
                    $root = Resolve-EngineRoot ([string]$entry.InstallLocation)
                    if ($root) { return $root }
                    $tried.Add("LauncherInstalled.dat UE_$association=$($entry.InstallLocation)")
                }
            }
        } catch { }
    }

    $default = "C:\Program Files\Epic Games\UE_$association"
    $root = Resolve-EngineRoot $default
    if ($root) { return $root }
    $tried.Add($default)

    throw ("Unreal Engine $association not found. Tried: " + ($tried -join "; ") +
        ". Pass -EngineDir 'C:\path\to\UE_5.8' or set the UE_ENGINE_DIR environment variable.")
}

# ---------------------------------------------------------------------------------------------------
# Report parsing
# ---------------------------------------------------------------------------------------------------

# EAutomationState order, for reports that store the state as a number.
$StateNames = @("NotRun", "InProcess", "Fail", "Success", "Skipped")
$EventTypeNames = @("Info", "Warning", "Error")

function Get-Prop($Object, [string]$Name, $Default = $null) {
    if ($null -eq $Object) { return $Default }
    $prop = $Object.PSObject.Properties | Where-Object { $_.Name -eq $Name } | Select-Object -First 1
    if ($null -eq $prop -or $null -eq $prop.Value) { return $Default }
    return $prop.Value
}

function Get-StateName($State) {
    if ($State -is [int] -or $State -is [long]) {
        if ($State -ge 0 -and $State -lt $StateNames.Count) { return $StateNames[$State] }
        return "State$State"
    }
    return [string]$State
}

function Get-EventTypeName($Type) {
    if ($Type -is [int] -or $Type -is [long]) {
        if ($Type -ge 0 -and $Type -lt $EventTypeNames.Count) { return $EventTypeNames[$Type] }
    }
    return [string]$Type
}

# One test's error and warning messages, as "Error: message (File.cpp:12)".
function Get-TestMessages($Test, [int]$Max = 25) {
    $messages = New-Object System.Collections.Generic.List[string]
    foreach ($entry in @(Get-Prop $Test "entries" @() | Where-Object { $null -ne $_ })) {
        $event = Get-Prop $entry "event"
        if ($null -eq $event) { continue }
        $type = Get-EventTypeName (Get-Prop $event "type" "")
        if ($type -ne "Error" -and $type -ne "Warning") { continue }
        $text = ([string](Get-Prop $event "message" "")).Trim()
        $file = [string](Get-Prop $entry "filename" "")
        $line = Get-Prop $entry "lineNumber" 0
        if ($file) { $text += " (" + (Split-Path $file -Leaf) + ":" + $line + ")" }
        $messages.Add("$type`: $text")
    }
    if ($messages.Count -gt $Max) {
        $extra = $messages.Count - $Max
        $trimmed = New-Object System.Collections.Generic.List[string]
        $trimmed.AddRange([string[]]($messages.GetRange(0, $Max)))
        $trimmed.Add("... and $extra more (see Saved\EditorTests\index.html or editor.log)")
        return $trimmed
    }
    return $messages
}

function Show-LogTail([string]$Path, [int]$Lines = 60) {
    if (Test-Path $Path) {
        Write-Summary ""
        Write-Summary "Last $Lines lines of $Path`:"
        foreach ($l in (Get-Content -Path $Path -Tail $Lines)) { Write-Summary "  $l" }
    }
}

# ---------------------------------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------------------------------

New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
$modeText = if ($Visual) { ", visual run (rendering on, screenshots in $ScreenshotDir)" } else { "" }
Write-Summary ("Crossguard editor tests, " + (Get-Date -Format "yyyy-MM-dd HH:mm:ss") + ", filter '$Filter'$modeText")

if (-not (Test-Path $ProjectFile)) {
    Write-Summary "SETUP ERROR: $ProjectFile not found." "Red"
    Exit-With 4
}

try {
    $EngineRoot = Find-EngineDir
} catch {
    Write-Summary ("SETUP ERROR: " + $_.Exception.Message) "Red"
    Exit-With 4
}
Write-Summary "Engine: $EngineRoot"

$BuildBat = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
$EditorCmd = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

# --- Build ---------------------------------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "Building CrossguardEditor Win64 Development (log: $BuildLog) ..."
    $buildStart = Get-Date
    Remove-Item -Path $BuildLog, $BuildErrLog -Force -ErrorAction SilentlyContinue
    # cmd /c ""bat" args": cmd strips the outer quotes, so paths with spaces stay quoted.
    $buildArgs = "/c `"`"$BuildBat`" CrossguardEditor Win64 Development `"-Project=$ProjectFile`" -WaitMutex`""
    $buildProc = Start-Process -FilePath "cmd.exe" -ArgumentList $buildArgs -NoNewWindow -PassThru `
        -RedirectStandardOutput $BuildLog -RedirectStandardError $BuildErrLog
    $null = $buildProc.Handle   # keeps ExitCode readable after exit (Windows PowerShell quirk)
    $buildProc.WaitForExit()
    $buildExit = $buildProc.ExitCode
    # One log: stderr (rare) appended to stdout.
    if ((Test-Path $BuildErrLog) -and (Get-Item $BuildErrLog).Length -gt 0) {
        Add-Content -Path $BuildLog -Value (Get-Content -Path $BuildErrLog)
    }
    $buildSeconds = [int]((Get-Date) - $buildStart).TotalSeconds
    if ($buildExit -ne 0) {
        Write-Summary "BUILD FAILED (exit code $buildExit, $buildSeconds s). Log: $BuildLog" "Red"
        $buildText = Get-Content -Path $BuildLog -ErrorAction SilentlyContinue
        if ($buildText -match "Live Coding") {
            Write-Summary "Hint: the editor is open with Live Coding. Close the editor (or press Ctrl+Alt+F11 there to compile) and run again." "Yellow"
        }
        $errors = @($buildText | Select-String -Pattern ': (fatal )?error ', 'error C\d{4}', 'error LNK\d+', '^\s*ERROR:', '^Error:', 'Unable to build' |
            ForEach-Object { $_.Line.Trim() } | Select-Object -Unique)
        if ($errors.Count -gt 0) {
            Write-Summary ""
            Write-Summary "Build errors (last $([Math]::Min(40, $errors.Count)) of $($errors.Count)):"
            foreach ($e in ($errors | Select-Object -Last 40)) { Write-Summary "  $e" }
        } else {
            Show-LogTail $BuildLog 40
        }
        Exit-With 2
    }
    Write-Summary "Build succeeded ($buildSeconds s)."
} else {
    Write-Summary "Build skipped (-SkipBuild)."
}

# --- Run the tests ------------------------------------------------------------------------------------
if (-not (Test-Path $EditorCmd)) {
    Write-Summary "SETUP ERROR: $EditorCmd not found." "Red"
    Exit-With 4
}

foreach ($stale in @($IndexJson, (Join-Path $ReportDir "index.html"), $EditorLog, $EditorErrLog)) {
    Remove-Item -Path $stale -Force -ErrorAction SilentlyContinue
}
if ($Visual) {
    Remove-Item -Path $ScreenshotDir -Recurse -Force -ErrorAction SilentlyContinue
}

# -nullrhi: no GPU, no drawing. The visual run leaves it out so the PIE windows render for screenshots.
$rhiArgs = @()
if (-not $Visual) { $rhiArgs = @("-nullrhi") }
$editorArgs = (@(
    "`"$ProjectFile`"",
    "-ExecCmds=`"Automation RunTests $Filter;Quit`"",
    "-unattended") + $rhiArgs + @("-nosplash", "-nosound", "-NoP4",
    "-stdout", "-FullStdOutLogOutput",
    "-ReportExportPath=`"$ReportDir`"",
    "-log"
)) -join " "

if ($Visual) { Write-Host "Running tests with rendering (log: $EditorLog) ..." } else { Write-Host "Running tests headlessly (log: $EditorLog) ..." }
Write-Host "  $EditorCmd $editorArgs"
$runStart = Get-Date
$proc = Start-Process -FilePath $EditorCmd -ArgumentList $editorArgs -NoNewWindow -PassThru `
    -RedirectStandardOutput $EditorLog -RedirectStandardError $EditorErrLog
$null = $proc.Handle   # keeps ExitCode readable after exit (Windows PowerShell quirk)
$finished = $proc.WaitForExit($TimeoutMinutes * 60 * 1000)
if (-not $finished) {
    try { $proc.Kill() } catch { }
    Write-Summary "TIMEOUT: the editor ran longer than $TimeoutMinutes minutes and was stopped." "Red"
}
$proc.WaitForExit()
$editorExit = $proc.ExitCode
$runSeconds = [int]((Get-Date) - $runStart).TotalSeconds

# --- Report ---------------------------------------------------------------------------------------
if (-not (Test-Path $IndexJson)) {
    Write-Summary "NO REPORT: $IndexJson was not written (editor exit code $editorExit, $runSeconds s)." "Red"
    $logText = Get-Content -Path $EditorLog -ErrorAction SilentlyContinue
    $completed = @($logText | Select-String -Pattern 'Test Completed\. Result=\{(\w+)\}.*Path=\{([^}]*)\}')
    if ($completed.Count -gt 0) {
        Write-Summary "Results found in the log:"
        foreach ($m in $completed) { Write-Summary ("  " + $m.Matches[0].Groups[1].Value.ToUpper() + "  " + $m.Matches[0].Groups[2].Value) }
    }
    if ($logText -match "No automation tests matched|Found 0 automation tests") {
        Write-Summary "No test matched '$Filter'. Was the editor built after the tests were added (Source\Crossguard\Private\Tests)?" "Yellow"
    }
    $crash = @($logText | Select-String -Pattern 'Fatal error|Assertion failed|Unhandled Exception|=== Critical error ===' | Select-Object -First 10)
    foreach ($c in $crash) { Write-Summary ("  " + $c.Line.Trim()) "Red" }
    Show-LogTail $EditorLog 60
    Exit-With 3
}

try {
    $report = [System.IO.File]::ReadAllText($IndexJson) | ConvertFrom-Json
} catch {
    Write-Summary ("BAD REPORT: cannot parse $IndexJson`: " + $_.Exception.Message) "Red"
    Exit-With 3
}

$tests = @(Get-Prop $report "tests" @() | Where-Object { $null -ne $_ })
$passed = @(); $failed = @(); $other = @()
foreach ($t in $tests) {
    $state = Get-StateName (Get-Prop $t "state" "")
    if ($state -eq "Success") { $passed += $t }
    elseif ($state -eq "Fail") { $failed += $t }
    else { $other += $t }
}

Write-Summary ""
$headline = "RESULT: $($passed.Count) passed, $($failed.Count) failed"
if ($other.Count -gt 0) { $headline += ", $($other.Count) not run/skipped" }
$headline += " ($($tests.Count) tests, $runSeconds s, editor exit code $editorExit)"
if ($failed.Count -gt 0 -or $tests.Count -eq 0) { Write-Summary $headline "Red" } else { Write-Summary $headline "Green" }

foreach ($t in ($tests | Sort-Object { [string](Get-Prop $_ "fullTestPath" "") })) {
    $name = [string](Get-Prop $t "fullTestPath" (Get-Prop $t "testDisplayName" "?"))
    $state = Get-StateName (Get-Prop $t "state" "")
    $duration = Get-Prop $t "duration" 0
    $messages = Get-TestMessages $t
    $label = switch ($state) { "Success" { "PASS" } "Fail" { "FAIL" } default { $state.ToUpper() } }
    $color = switch ($state) { "Success" { "" } "Fail" { "Red" } default { "Yellow" } }
    Write-Summary ("{0,-8}{1}  ({2:0.00} s)" -f $label, $name, [double]$duration) $color
    foreach ($m in $messages) { Write-Summary "        $m" }
}

if ($Visual) {
    $shots = @()
    if (Test-Path $ScreenshotDir) { $shots = @(Get-ChildItem -Path $ScreenshotDir -Recurse -Filter *.png | Sort-Object FullName) }
    Write-Summary ""
    Write-Summary "Screenshots ($($shots.Count)):"
    foreach ($shot in $shots) { Write-Summary ("  " + $shot.FullName.Substring($ScreenshotDir.Length + 1)) }
}

if ($tests.Count -eq 0) {
    if ($Visual) {
        Write-Summary "No test matched '$Filter'. Visual tests only exist when the editor can render; was it built after Source\Crossguard\Private\Tests changed?" "Yellow"
    } else {
        Write-Summary "No test matched '$Filter'. Was the editor built after the tests were added (Source\Crossguard\Private\Tests)?" "Yellow"
    }
    Exit-With 1
}
if ($failed.Count -gt 0) {
    Write-Summary ""
    Write-Summary "Full log: $EditorLog  |  HTML report: $(Join-Path $ReportDir 'index.html')"
    Exit-With 1
}
Exit-With 0
