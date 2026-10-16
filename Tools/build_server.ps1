<#
.SYNOPSIS
    Builds the CrossguardServer target (dedicated server) for Win64 or Linux and, with -Package,
    cooks and stages a runnable server build. Needs a source build of Unreal Engine 5.8.

.DESCRIPTION
    The Epic launcher engine cannot build Server targets: this script stops with exit 4 if the engine
    it finds is an installed build (Engine\Build\InstalledBuild.txt). Set-up: docs/dedicated-server-plan.md.

    Without -Package: Build.bat CrossguardServer <Platform> <Configuration> (compile check only).
    With -Package: first builds CrossguardEditor with the same engine (the cook runs the project's
    editor modules; skip with -SkipEditorBuild), then RunUAT BuildCookRun -server -noclient (or with a
    Win64 client too, -WithClient), cooks -Maps, stages, paks and archives to -ArchiveDir.

    Logs: Saved\ServerBuild\build.log (errors: errors.txt), package.log.
    Exit codes: 0 done; 2 build failed; 3 cook / package failed; 4 bad set-up (no source engine, no
    Linux toolchain, project not found).

    Building CrossguardEditor with the source engine overwrites this folder's editor DLLs: the
    launcher editor then asks to rebuild. Use a separate build copy of the repository for -Package
    (docs/dedicated-server-plan.md, "Two engines side by side").

.PARAMETER EngineDir
    Source engine root (the folder holding Engine\). Default: $env:UE_SOURCE_ENGINE_DIR, else
    $env:UE_ENGINE_DIR, else the engine the .uproject's EngineAssociation points to.

.PARAMETER Platform
    Win64 (default) or Linux (needs the cross-compile toolchain, LINUX_MULTIARCH_ROOT).

.PARAMETER Configuration
    Development (default), DebugGame, Test or Shipping.

.PARAMETER Package
    Cook, stage, pak and archive a runnable server (RunUAT BuildCookRun).

.PARAMETER WithClient
    With -Package: also package a Win64 game client from the same engine (a packaged server only
    accepts clients built from the same engine and project version).

.PARAMETER ArchiveDir
    Where -Package puts the build. Default Saved\Packages\Server (WindowsServer\ or LinuxServer\ inside).

.PARAMETER Maps
    Maps to cook, '+'-separated. Default: hub, raid sandbox and main menu.

.PARAMETER SkipEditorBuild
    With -Package: do not build CrossguardEditor first.

.PARAMETER MaxErrors
    How many error lines to print. Default 40.

.EXAMPLE
    Tools\build_server.bat -EngineDir D:\UE\UE_5.8_Source
.EXAMPLE
    Tools\build_server.bat -Package -Platform Linux
#>
[CmdletBinding()]
param(
    [string]$EngineDir = "",
    [ValidateSet("Win64", "Linux")]
    [string]$Platform = "Win64",
    [ValidateSet("Development", "DebugGame", "Test", "Shipping")]
    [string]$Configuration = "Development",
    [switch]$Package,
    [switch]$WithClient,
    [string]$ArchiveDir = "",
    [string]$Maps = "/Game/Maps/L_Hub+/Game/Maps/Dev/L_DevSandbox+/Game/Maps/L_MainMenu",
    [switch]$SkipEditorBuild,
    [int]$MaxErrors = 40
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "ue_common.ps1")

$OutDir = Join-Path $CgRepoRoot "Saved\ServerBuild"
$BuildLog = Join-Path $OutDir "build.log"
$ErrorsFile = Join-Path $OutDir "errors.txt"
$PackageLog = Join-Path $OutDir "package.log"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (-not $ArchiveDir) { $ArchiveDir = Join-Path $CgRepoRoot "Saved\Packages\Server" }

function Write-SetupError([string]$Message) {
    Write-Host "SETUP ERROR: $Message" -ForegroundColor Red
    exit 4
}

function Write-BuildFailure($Result, [string]$Log, [string]$What) {
    Write-Host ("{0} FAILED (exit code {1}, {2} s, {3} error lines). Log: {4}" -f $What, $Result.ExitCode, $Result.Seconds, $Result.Errors.Count, $Log) -ForegroundColor Red
    if ($Result.Errors.Count -gt 0) {
        $shown = [Math]::Min($MaxErrors, $Result.Errors.Count)
        Write-Host "First $shown of $($Result.Errors.Count) (all in $ErrorsFile):"
        foreach ($e in ($Result.Errors | Select-Object -First $MaxErrors)) { Write-Host "  $e" }
    } else {
        Write-Host "No error lines recognised; last 40 lines of the log:"
        foreach ($l in (Get-Content -Path $Log -Tail 40 -ErrorAction SilentlyContinue)) { Write-Host "  $l" }
    }
}

if (-not (Test-Path $CgProjectFile)) { Write-SetupError "$CgProjectFile not found." }
if (-not (Test-Path (Join-Path $CgRepoRoot "Source\CrossguardServer.Target.cs"))) {
    Write-SetupError "Source\CrossguardServer.Target.cs is missing (Type = TargetType.Server)."
}

# Source engine: -EngineDir, then UE_SOURCE_ENGINE_DIR, then the usual lookup.
if (-not $EngineDir -and $env:UE_SOURCE_ENGINE_DIR) { $EngineDir = $env:UE_SOURCE_ENGINE_DIR }
try {
    $EngineRoot = Find-CgEngineDir $EngineDir
} catch {
    Write-SetupError $_.Exception.Message
}
$EngineVersion = Get-CgEngineVersion $EngineRoot
Write-Host "Engine: $EngineRoot ($EngineVersion)"
if (Test-CgInstalledEngine $EngineRoot) {
    Write-SetupError ("$EngineRoot is an installed (Epic launcher) engine: it cannot build Server targets. " +
        "Build UE 5.8 from source (docs/dedicated-server-plan.md, part 1) and pass -EngineDir <source engine> " +
        "or set UE_SOURCE_ENGINE_DIR.")
}
if (-not $EngineVersion.StartsWith("5.8")) {
    Write-Host "WARNING: engine version '$EngineVersion' is not 5.8." -ForegroundColor Yellow
}

if ($Platform -eq "Linux") {
    $toolchain = $env:LINUX_MULTIARCH_ROOT
    if (-not $toolchain -or -not (Test-Path $toolchain)) {
        Write-SetupError ("LINUX_MULTIARCH_ROOT is not set or missing ('$toolchain'). Install the UE 5.8 cross-compile " +
            "toolchain (v26_clang-20.1.8-rockylinux8), open a new terminal and run GenerateProjectFiles.bat in the engine " +
            "(docs/dedicated-server-plan.md, part 2).")
    }
    Write-Host "Linux toolchain: $toolchain"
}

$ServerFolder = if ($Platform -eq "Linux") { "LinuxServer" } else { "WindowsServer" }

if (-not $Package) {
    Write-Host "Building CrossguardServer $Platform $Configuration (log: $BuildLog) ..."
    $result = Invoke-CgTargetBuild $EngineRoot "CrossguardServer" $Platform $Configuration $BuildLog
    [System.IO.File]::WriteAllLines($ErrorsFile, [string[]]$result.Errors.ToArray(), (New-Object System.Text.UTF8Encoding($false)))
    if ($result.ExitCode -eq 0) {
        Write-Host ("BUILD OK ({0} s, {1} warnings). Binary: Binaries\{2}\CrossguardServer*" -f $result.Seconds, $result.Warnings, $Platform) -ForegroundColor Green
        exit 0
    }
    Write-BuildFailure $result $BuildLog "BUILD"
    exit 2
}

# --- Package ---------------------------------------------------------------------------------------

$association = Get-CgEngineAssociation
if ($association -eq "5.8") {
    Write-Host ("NOTE: Crossguard.uproject points at the launcher engine (EngineAssociation 5.8). The cook uses " +
        "$EngineRoot anyway; building CrossguardEditor with it replaces this folder's editor DLLs.") -ForegroundColor Yellow
}

if (-not $SkipEditorBuild) {
    if (Test-CgEditorRunning) {
        Write-SetupError "An UnrealEditor process is running. Close the editor before packaging."
    }
    Write-Host "Building CrossguardEditor Win64 Development with the source engine (for the cook; log: $BuildLog) ..."
    $editor = Invoke-CgEditorBuild $EngineRoot $BuildLog
    [System.IO.File]::WriteAllLines($ErrorsFile, [string[]]$editor.Errors.ToArray(), (New-Object System.Text.UTF8Encoding($false)))
    if ($editor.ExitCode -ne 0) {
        Write-BuildFailure $editor $BuildLog "EDITOR BUILD"
        exit 2
    }
    Write-Host ("Editor built ({0} s)." -f $editor.Seconds) -ForegroundColor Green
}

$runUat = Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
$uatArgs = @(
    "BuildCookRun",
    "`"-project=$CgProjectFile`"",
    "-noP4", "-unattended", "-utf8output", "-nocompileeditor",
    "-server", "-serverplatform=$Platform", "-serverconfig=$Configuration",
    "-build", "-cook", "-stage", "-pak", "-archive",
    "`"-archivedirectory=$ArchiveDir`"",
    "-map=$Maps"
)
if ($WithClient) {
    $uatArgs += @("-platform=Win64", "-clientconfig=$Configuration")
} else {
    $uatArgs += "-noclient"
}

Write-Host "Packaging CrossguardServer $Platform $Configuration to $ArchiveDir (log: $PackageLog) ..."
Remove-Item -Path $PackageLog, "$PackageLog.stderr" -Force -ErrorAction SilentlyContinue
$start = Get-Date
$cmdArgs = "/c `"`"$runUat`" " + ($uatArgs -join " ") + "`""
$proc = Start-Process -FilePath "cmd.exe" -ArgumentList $cmdArgs -NoNewWindow -PassThru `
    -RedirectStandardOutput $PackageLog -RedirectStandardError "$PackageLog.stderr"
$null = $proc.Handle   # keeps ExitCode readable after exit (Windows PowerShell quirk)
$proc.WaitForExit()
if ((Test-Path "$PackageLog.stderr") -and (Get-Item "$PackageLog.stderr").Length -gt 0) {
    Add-Content -Path $PackageLog -Value (Get-Content -Path "$PackageLog.stderr")
}
Remove-Item -Path "$PackageLog.stderr" -Force -ErrorAction SilentlyContinue
$seconds = [int]((Get-Date) - $start).TotalSeconds

$text = @(Get-Content -Path $PackageLog -ErrorAction SilentlyContinue)
$errors = Get-CgBuildErrors $text
[System.IO.File]::WriteAllLines($ErrorsFile, [string[]]$errors.ToArray(), (New-Object System.Text.UTF8Encoding($false)))
if ($proc.ExitCode -eq 0) {
    Write-Host ("PACKAGE OK ({0} s). Server: {1}" -f $seconds, (Join-Path $ArchiveDir $ServerFolder)) -ForegroundColor Green
    if ($WithClient) { Write-Host ("Client: {0}" -f (Join-Path $ArchiveDir "Windows")) -ForegroundColor Green }
    exit 0
}
Write-BuildFailure @{ ExitCode = $proc.ExitCode; Seconds = $seconds; Errors = $errors } $PackageLog "PACKAGE"
exit 3
